#include "ioapic.h"
#include "../../../core/kernel_api.h"
#include "../mmu/mmu.h"
#include "../../../cpu/paging.h"

#define IOAPIC_VIRT_BASE 0xFFFFA00000101000ULL

static volatile uint32_t *ioapic_base = NULL;

static irq_registration_t irq_registry[MAX_REGISTERED_IRQS];
static int irq_registry_size = 0;

void irq_registry_add(uint8_t irq, uint8_t vector,
                      uint8_t dest, uint8_t masked,
                      const char *name) {
    if (irq_registry_size >= MAX_REGISTERED_IRQS) return;
    irq_registry[irq_registry_size++] =
        (irq_registration_t){ irq, vector, dest, masked, name };
}

int irq_registry_count(void) {
    return irq_registry_size;
}

irq_registration_t *irq_registry_get(int index) {
    if (index < 0 || index >= irq_registry_size) return NULL;
    return &irq_registry[index];
}

int irq_registry_verify_all(void) {
    int ok = 1;
    for (int i = 0; i < irq_registry_size; i++) {
        irq_registration_t *r = &irq_registry[i];
        uint32_t lo = ioapic_read_irq_lo(r->irq);
        uint8_t  actual_vector  = lo & 0xFF;
        uint8_t  actual_masked  = (lo >> 16) & 1;

        if (actual_masked != r->should_be_masked) {
            kprint("[IRQ AUDIT] FAIL: IRQ");
            char s[16]; int_to_ascii(r->irq, s); kprint(s);
            kprint(" ("); kprint(r->name); kprint(") mask=");
            int_to_ascii(actual_masked, s); kprint(s);
            kprint(" want=");
            int_to_ascii(r->should_be_masked, s); kprint(s);
            kprint("\n");
            ok = 0;
        }
        if (!r->should_be_masked &&
            actual_vector != r->vector) {
            kprint("[IRQ AUDIT] FAIL: IRQ");
            char s[16]; int_to_ascii(r->irq, s); kprint(s);
            kprint(" ("); kprint(r->name); kprint(") vector=");
            int_to_ascii(actual_vector, s); kprint(s);
            kprint(" want=");
            int_to_ascii(r->vector, s); kprint(s);
            kprint("\n");
            ok = 0;
        }
        if (ok) {
            kprint("[IRQ AUDIT] OK: IRQ");
            char s[16]; int_to_ascii(r->irq, s); kprint(s);
            kprint(" ("); kprint(r->name); kprint(") vector=");
            int_to_ascii(r->vector, s); kprint(s);
            kprint(r->should_be_masked ? " (masked)\n" : " (active)\n");
        }
    }
    return ok;
}

static void ioapic_write(uint8_t reg, uint32_t val) {
    if (!ioapic_base) return;
    ioapic_base[0] = reg;
    ioapic_base[4] = val;
}

uint32_t ioapic_read(uint8_t reg) {
    if (!ioapic_base) return 0;
    ioapic_base[0] = reg;
    return ioapic_base[4];
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) {
    uint8_t reg_lo = 0x10 + (irq * 2);
    uint8_t reg_hi = 0x11 + (irq * 2);
    
    uint32_t hi = (uint32_t)dest_apic_id << 24;
    uint32_t lo = vector; // fixed, physical, unmasked, edge
    
    ioapic_write(reg_lo, lo);
    ioapic_write(reg_hi, hi);
}

void ioapic_mask_irq(uint8_t irq) {
    uint8_t reg_lo = 0x10 + (irq * 2);
    uint32_t lo = ioapic_read(reg_lo);
    lo |= (1 << 16); // Set mask bit
    ioapic_write(reg_lo, lo);
}

uint32_t ioapic_read_irq_lo(uint8_t irq) {
    return ioapic_read(0x10 + (irq * 2));
}

uint32_t ioapic_read_irq_hi(uint8_t irq) {
    return ioapic_read(0x11 + (irq * 2));
}

void ioapic_init(void) {
    // smp_info from ACPI states IOAPIC is at 0xFEC00000
    uint64_t phys = 0xFEC00000;
    mmu_map_page(kernel_directory, IOAPIC_VIRT_BASE, phys, MMU_PRESENT | MMU_WRITABLE | MMU_PCD | MMU_PWT);
    ioapic_base = (volatile uint32_t *)IOAPIC_VIRT_BASE;
    
    uint32_t id_reg = ioapic_read(0x00);
    uint32_t ver_reg = ioapic_read(0x01);
    
    char s[16];
    kprint("[IOAPIC] ID=0x");
    hex_to_ascii((id_reg >> 24) & 0xF, s); kprint(s);
    kprint(" version=0x");
    hex_to_ascii(ver_reg & 0xFF, s); kprint(s);
    kprint(" entries=");
    int_to_ascii(((ver_reg >> 16) & 0xFF) + 1, s); kprint(s);
    kprint("\n");
    
    // Route and register IRQ1 keyboard
    ioapic_route_irq(1, 33, 0);
    irq_registry_add(1, 33, 0, 0, "keyboard/PS2");
    kprint("[IOAPIC] IRQ1 (keyboard) routed to vector 33, BSP\n");

    // Route and register IRQ4 UART
    ioapic_route_irq(4, 36, 0);
    irq_registry_add(4, 36, 0, 0, "UART COM1");
    kprint("[IOAPIC] IRQ4 (UART COM1) routed to vector 36, BSP\n");
    
    // Mask and register IRQ0 (PIT) since LAPIC timer takes over
    ioapic_mask_irq(0);
    irq_registry_add(0, 32, 0, 1, "PIT (masked)");
    kprint("[IOAPIC] IRQ0 PIT masked\n");
}
