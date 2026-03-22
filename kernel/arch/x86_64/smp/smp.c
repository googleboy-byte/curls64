#include "smp.h"
#include "trampoline_blob.h"
#include "../../../../libc/mem.h"
#include "../../../include/cpu_local.h"
#include "../acpi/acpi.h"
#include "../apic/lapic.h"
#include "../../../../libc/kheap.h"
 // removed vga_print.h
#include "../../../cpu/isr.h"

#define PML4_OFFSET  16
#define GDT_OFFSET   20
#define STACK_OFFSET 30
#define ENTRY_OFFSET 38
#define CPUID_OFFSET 46

extern unsigned char build_trampoline_bin[];
extern unsigned int build_trampoline_bin_len;

extern void int_to_ascii(int n, char *str);
extern void hex_to_ascii(uint32_t n, char *str);
extern void kprint(const char *str);

volatile uint32_t ap_ready_flags = 0;

static inline uint64_t get_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
extern void cpu_init(int cpu_id);
extern void lapic_init(void);

// AP entry function called from assembly trampoline
void ap_entry(int cpu_id) {
    kprint("AP inside C!\n");
    *((volatile uint16_t*)0xffff8000000b8000 + (cpu_id * 80)) = 0x0F00 | ('0' + cpu_id);
    // 1. Initialize this AP's cpu_local
    cpu_init(cpu_id);

    // 2. Initialize LAPIC on this AP
    lapic_enable_via_msr(0xFEE00000);
    // For now we can call lapic_init() which does LAPIC enabling. Wait, we need to skip PIT calibration.
    // We already have ticks_per_interval computed on BSP, so we can just set up the APIC timer.
    lapic_write(LAPIC_TIMER_DIV, 0x3);
    extern uint32_t ticks_per_interval;
    lapic_write(LAPIC_TIMER_INIT, ticks_per_interval);
    lapic_write(LAPIC_TIMER_LVT, 0x40 | 0x20000); // Vector 0x40, Periodic

    // Enable LAPIC (Spurious register)
    lapic_write(0xF0, 0x100 | 0xFF);

    // 3. Enable interrupts
    extern void set_idt(void);
    set_idt();
    asm volatile("sti");

    // 4. Signal BSP that this AP is ready
    ap_ready_flags |= (1 << cpu_id);

    // 5. Enter scheduler idle loop
    while (1) {
        asm volatile("hlt");
    }
}

extern mmu_context_t *kernel_directory;
extern void pmm_clear_frame(uint32_t frame);
#define PHYSMAP_BASE 0xffff800000000000ULL
#define TRAMPOLINE_PHYS 0x70000ULL
#define TRAMPOLINE_VIRT (PHYSMAP_BASE + TRAMPOLINE_PHYS)
#define BREADCRUMB_PHYS 0x6000ULL
#define BREADCRUMB_VIRT (PHYSMAP_BASE + BREADCRUMB_PHYS)

void smp_copy_trampoline(void) {
    memory_copy(build_trampoline_bin, (uint8_t*)TRAMPOLINE_VIRT, build_trampoline_bin_len);

    uint8_t *check = (uint8_t*)TRAMPOLINE_VIRT;
    char s[16];
    kprint("[SMP] Trampoline[0..3]: ");
    hex_to_ascii(check[0], s); kprint(s); kprint(" ");
    hex_to_ascii(check[1], s); kprint(s); kprint(" ");
    hex_to_ascii(check[2], s); kprint(s); kprint(" ");
    hex_to_ascii(check[3], s); kprint(s); kprint("\n");
}

void lapic_send_ipi(uint8_t dest, uint32_t cmd) {
    // Step 1: Write destination to ICR_HIGH
    // Destination APIC ID goes in bits 31:24
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)dest << 24);
    
    // Step 2: Write command to ICR_LOW — this FIRES the IPI
    lapic_write(LAPIC_ICR_LOW, cmd);
    
    // Step 3: Wait for delivery (bit 12 = delivery status)
    int timeout = 100000;
    while ((lapic_read(LAPIC_ICR_LOW) & (1 << 12)) && --timeout);
    if (!timeout) {
        kprint("[LAPIC] IPI delivery timeout to APIC ");
        char b[16];
        int_to_ascii(dest, b); kprint(b);
        kprint("\n");
    }
}

void smp_start_aps(void) {
    smp_copy_trampoline();
    
    smp_info_t *info = acpi_get_smp_info();

    extern gdt_ptr_t gdt_ptr;

    kprint("[SMP] Starting APs...\n");

    uint8_t *t = (uint8_t*)TRAMPOLINE_VIRT;
    kprint("[SMP] Trampoline bytes at 0x70000:\n");
    for (int i = 0; i < 32; i++) {
        char h[3];
        h[0] = "0123456789ABCDEF"[(t[i] >> 4) & 0xF];
        h[1] = "0123456789ABCDEF"[t[i] & 0xF];
        h[2] = 0;
        kprint(h); kprint(" ");
        if ((i + 1) % 16 == 0) kprint("\n");
    }

    for (int i = 0; i < info->ap_count; i++) {
        uint8_t apic_id = info->ap_apic_ids[i];
        int     cpu_id  = i + 1;  // BSP is 0

        void *stack = kmalloc(8192, 4096, 0);
        uint64_t stack_top = (uint64_t)stack + 8192;

        *(uint32_t*)(TRAMPOLINE_VIRT + PML4_OFFSET)    = (uint32_t)get_cr3();
        *(uint16_t*)(TRAMPOLINE_VIRT + GDT_OFFSET)     = gdt_ptr.limit;
        *(uint64_t*)(TRAMPOLINE_VIRT + GDT_OFFSET + 2) = gdt_ptr.base;
        *(uint64_t*)(TRAMPOLINE_VIRT + STACK_OFFSET)   = stack_top;
        *(uint64_t*)(TRAMPOLINE_VIRT + ENTRY_OFFSET)   = (uint64_t)&ap_ready_flags;
        *(uint32_t*)(TRAMPOLINE_VIRT + CPUID_OFFSET)   = cpu_id;

        *((volatile uint16_t*)BREADCRUMB_VIRT) = 0x0000; // Reset breadcrumb

        // INIT Assert
        lapic_send_ipi(apic_id, 0x00004500);
        for(volatile int d=0; d<100000; d++); // delay
        // INIT Deassert (Level = 0, Trigger = 1 -> 0x8500)
        lapic_send_ipi(apic_id, 0x00008500);
        for(volatile int d=0; d<100000; d++); // delay

        // SIPI vec 0x70
        lapic_send_ipi(apic_id, 0x00004670);
        for(volatile int d=0; d<10000; d++); // delay

        // Second SIPI vec 0x70
        lapic_send_ipi(apic_id, 0x00004670);
        for(volatile int d=0; d<10000; d++); // delay

        int timeout = 500000;
        while (!(ap_ready_flags & (1 << cpu_id)) && timeout-- > 0) {
            asm volatile("pause");
        }

        uint16_t *breadcrumb = (uint16_t*)BREADCRUMB_VIRT;
        kprint("[SMP] AP breadcrumb: ");
        char b[16]; int_to_ascii(*breadcrumb, b); kprint(b); kprint(" (expect CAFE)\n");

        if (ap_ready_flags & (1 << cpu_id)) {
            char b[16];
            kprint("[SMP] AP ");
            int_to_ascii(cpu_id, b); kprint(b);
            kprint(" (APIC ");
            int_to_ascii(apic_id, b); kprint(b);
            kprint(") online\n");
        } else {
            char b[16];
            kprint("[SMP] AP ");
            int_to_ascii(cpu_id, b); kprint(b);
            kprint(" TIMEOUT\n");
        }
    }
}
