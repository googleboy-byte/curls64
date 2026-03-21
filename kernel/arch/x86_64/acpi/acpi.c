#include "acpi.h"
#include "../../../core/kernel_api.h"
#include <stddef.h>

typedef struct {
    char     signature[8];   // "RSD PTR "
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;   // physical address of RSDT
} __attribute__((packed)) rsdp_t;

typedef struct {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

typedef struct {
    uint8_t type;        // 0
    uint8_t length;      // 8
    uint8_t acpi_id;
    uint8_t apic_id;
    uint32_t flags;      // bit 0 = enabled
} __attribute__((packed)) madt_lapic_t;

typedef struct {
    uint8_t  type;       // 1
    uint8_t  length;     // 12
    uint8_t  io_apic_id;
    uint8_t  reserved;
    uint32_t io_apic_addr;
    uint32_t gsi_base;
} __attribute__((packed)) madt_ioapic_t;

static smp_info_t cpu_info = {0};

smp_info_t *acpi_get_smp_info(void) {
    return &cpu_info;
}

static int check_signature(const char *sig1, const char *sig2, int len) {
    for (int i = 0; i < len; i++) {
        if (sig1[i] != sig2[i]) return 0;
    }
    return 1;
}

static rsdp_t* find_rsdp(void) {
    uint16_t ebda_segment = *((uint16_t*)0x40E);
    uint32_t ebda_base = ebda_segment << 4;
    
    for (uint32_t i = ebda_base; i < ebda_base + 1024; i += 16) {
        if (check_signature((char*)((uintptr_t)i), "RSD PTR ", 8)) {
            uint8_t sum = 0;
            for (int j = 0; j < 20; j++) sum += ((uint8_t*)(uintptr_t)i)[j];
            if (sum == 0) return (rsdp_t*)(uintptr_t)i;
        }
    }
    
    for (uint32_t i = 0xE0000; i < 0x100000; i += 16) {
        if (check_signature((char*)((uintptr_t)i), "RSD PTR ", 8)) {
            uint8_t sum = 0;
            for (int j = 0; j < 20; j++) sum += ((uint8_t*)(uintptr_t)i)[j];
            if (sum == 0) return (rsdp_t*)(uintptr_t)i;
        }
    }
    
    return NULL;
}

#include "../mmu/mmu.h"
#include "../../../cpu/paging.h"

static void* phys_to_virt_mapped(uint32_t phys) {
    uint64_t virt = 0xffff800000000000ULL + phys;
    uint64_t aligned_phys = phys & ~0xFFFULL;
    uint64_t aligned_virt = virt & ~0xFFFULL;
    
    // Assure pages are mapped in case PHYSMAP fallback didn't cover them
    mmu_map_page(kernel_directory, aligned_virt, aligned_phys, MMU_WRITABLE);
    mmu_map_page(kernel_directory, aligned_virt + 0x1000, aligned_phys + 0x1000, MMU_WRITABLE);
    
    return (void*)virt;
}

void acpi_parse(void) {
    rsdp_t *rsdp = find_rsdp();
    if (!rsdp) {
        kprint("[ACPI] RSDP not found!\n");
        return;
    }
    
    acpi_header_t *rsdt = (acpi_header_t*)phys_to_virt_mapped(rsdp->rsdt_address);
    if (!check_signature(rsdt->signature, "RSDT", 4)) {
        kprint("[ACPI] Invalid RSDT signature\n");
        return;
    }
    
    int entries = (rsdt->length - sizeof(acpi_header_t)) / 4;
    uint32_t *table_ptrs = (uint32_t*)((uintptr_t)rsdt + sizeof(acpi_header_t));
    
    acpi_header_t *madt = NULL;
    for (int i = 0; i < entries; i++) {
        acpi_header_t *header = (acpi_header_t*)phys_to_virt_mapped(table_ptrs[i]);
        if (check_signature(header->signature, "APIC", 4)) {
            madt = header;
            break;
        }
    }
    
    if (!madt) {
        kprint("[ACPI] MADT not found in RSDT\n");
        return;
    }
    
    uint32_t *lapic_addr_ptr = (uint32_t*)((uintptr_t)madt + sizeof(acpi_header_t));
    uint32_t *flags_ptr = lapic_addr_ptr + 1;
    
    cpu_info.lapic_base = *lapic_addr_ptr;
    
    uint8_t *entry_ptr = (uint8_t*)(flags_ptr + 1);
    uint8_t *madt_end = (uint8_t*)((uintptr_t)madt + madt->length);
    
    int found_bsp = 0;
    
    while (entry_ptr < madt_end) {
        uint8_t type = entry_ptr[0];
        uint8_t len = entry_ptr[1];
        
        if (type == 0) {
            madt_lapic_t *lapic = (madt_lapic_t*)entry_ptr;
            if (lapic->flags & 1) { // Enabled
                if (!found_bsp) {
                    cpu_info.bsp_apic_id = lapic->apic_id;
                    found_bsp = 1;
                } else {
                    if (cpu_info.ap_count < 16) {
                        cpu_info.ap_apic_ids[cpu_info.ap_count++] = lapic->apic_id;
                    }
                }
            }
        } else if (type == 1) {
            madt_ioapic_t *ioapic = (madt_ioapic_t*)entry_ptr;
            cpu_info.ioapic_base = ioapic->io_apic_addr;
        }
        
        entry_ptr += len;
    }
    
    char s[32];
    kprint("[ACPI] LAPIC base: ");
    hex_to_ascii(cpu_info.lapic_base, s);
    kprint(s);
    kprint("\n");
    
    kprint("[ACPI] IOAPIC base: ");
    hex_to_ascii(cpu_info.ioapic_base, s);
    kprint(s);
    kprint("\n");
    
    kprint("[ACPI] Found ");
    int_to_ascii(cpu_info.ap_count, s);
    kprint(s);
    kprint(" APs: APIC IDs [");
    
    for (int i = 0; i < cpu_info.ap_count; i++) {
        int_to_ascii(cpu_info.ap_apic_ids[i], s);
        kprint(s);
        if (i < cpu_info.ap_count - 1) kprint(", ");
    }
    kprint("]\n");
}
