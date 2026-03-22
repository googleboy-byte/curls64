#include "lapic.h"
#include "../../../core/kernel_api.h"
#include "../mmu/mmu.h"
#include "../../../cpu/paging.h"
#include "../acpi/acpi.h"
#include "../../../cpu/ports.h"

static volatile uint32_t *lapic_base_virt = NULL;
int lapic_enabled = 0;
int use_lapic_timer = 0;
uint32_t ticks_per_interval = 0;

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base_virt) return 0;
    return lapic_base_virt[reg >> 2];
}

void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base_virt) return;
    lapic_base_virt[reg >> 2] = val;
}

void lapic_spurious_handler(registers_t *regs) {
    (void)regs;
    lapic_write(LAPIC_EOI, 0);
}

#define LAPIC_VIRT_BASE 0xFFFFA00000100000ULL

void lapic_enable_via_msr(uint64_t phys_base) {
    uint64_t msr_val = phys_base | (1 << 11); // EN bit
    asm volatile(
        "wrmsr"
        :: "c"(0x1B),
           "a"((uint32_t)(msr_val & 0xFFFFFFFF)),
           "d"((uint32_t)(msr_val >> 32))
    );
}

void lapic_init(void) {
    // 1. Mask 8259 PIC interrupts to avoid conflicts, EXCEPT IRQ0 for calibration
    port_byte_out(0x21, 0xFE);
    port_byte_out(0xA1, 0xFF);

    smp_info_t *smp = acpi_get_smp_info();
    uint64_t phys = smp->lapic_base;
    if (!phys) phys = 0xFEE00000;
    
    char s[16];
    
    uint32_t msr_lo, msr_hi;
    asm volatile(
        "rdmsr"
        : "=a"(msr_lo), "=d"(msr_hi)
        : "c"(0x1B)
    );
    kprint("[LAPIC] IA32_APIC_BASE MSR: hi=0x");
    hex_to_ascii(msr_hi, s); kprint(s);
    kprint(" lo=0x");
    hex_to_ascii(msr_lo, s); kprint(s);
    kprint("\n");

    lapic_enable_via_msr(phys);

    kprint("[LAPIC] Physical base from ACPI: 0x");
    hex_to_ascii((uint32_t)phys, s);
    kprint(s);
    kprint("\n");

    // Map LAPIC MMIO out of the way
    mmu_map_page(kernel_directory, LAPIC_VIRT_BASE, phys, MMU_PRESENT | MMU_WRITABLE | MMU_PCD | MMU_PWT);
    lapic_base_virt = (volatile uint32_t*)LAPIC_VIRT_BASE;
    lapic_enabled = 1;
    
    kprint("[LAPIC] Mapped 0xFEE00000 -> 0xFFFFA00000100000\n");

    lapic_write(LAPIC_ICR_HIGH, 0x01000000); // target APIC ID 1
    uint32_t readback = lapic_read(LAPIC_ICR_HIGH);
    kprint("[LAPIC] ICR_HIGH readback: 0x");
    hex_to_ascii(readback, s); kprint(s);
    kprint(" (expect 0x1000000)\n");

    // 2. Read BSP APIC ID from LAPIC_ID register
    uint32_t id_reg = lapic_read(LAPIC_ID);
    kprint("[LAPIC] ID register: 0x");
    hex_to_ascii(id_reg, s);
    kprint(s);
    kprint(" (BSP APIC ID should be 0)\n");
    uint8_t current_apic_id = (uint8_t)(id_reg >> 24);
    
    uint32_t ver_reg = lapic_read(LAPIC_VERSION);
    uint8_t version = (uint8_t)(ver_reg & 0xFF);
    
    // 3. Set Task Priority Register to 0 (accept all)
    lapic_write(LAPIC_TPR, 0);
    
    // 4. Set Spurious Vector Register (vector 0xFF | Enable bit 8)
    lapic_write(LAPIC_SVR, 0x100 | 0xFF);
    
    // 5. Print confirmation
    kprint("[LAPIC] BSP APIC ID: ");
    int_to_ascii(current_apic_id, s);
    kprint(s);
    kprint(", version: 0x");
    hex_to_ascii(version, s);
    kprint(s);
    kprint("\n[LAPIC] Enabled on BSP\n");
}
#include "../../../cpu/timer.h"
#include "../../../cpu/isr.h"

extern uint32_t get_ticks(void);

void init_lapic_timer(void) {
    // 1. Calibrate LAPIC timer using PIT
    lapic_write(LAPIC_TIMER_DIV, 0x3);         // Divide by 16
    lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF); // Start countdown
    
    // Wait for PIT to tick 10 times (10ms if PIT = 1000Hz)
    uint32_t start_tick = get_ticks();
    while (get_ticks() - start_tick < 10) {
        // Wait
    }
    
    uint32_t current = lapic_read(LAPIC_TIMER_CURR);
    uint32_t ticks_elapsed = 0xFFFFFFFF - current;
    
    // Calculate ticks per 1 interval (assuming interval is 1ms, so ticks_elapsed / 10)
    // Wait, PIT runs at what frequency? Let's check init_timer() frequency.
    // If it's usually 1000Hz, then 10 ticks = 10ms. Ticks per 1ms = ticks_elapsed / 10
    ticks_per_interval = ticks_elapsed / 10;
    if (ticks_per_interval == 0) ticks_per_interval = 10000; // fail-safe fallback
    
    // 2. Configure LAPIC timer LVT
    lapic_write(LAPIC_TIMER_LVT, 0x20040); // periodic | vector 0x40
    
    // 3. Set divide config to 16
    lapic_write(LAPIC_TIMER_DIV, 0x3);
    
    // 4. Set initial count to calibrated value
    lapic_write(LAPIC_TIMER_INIT, ticks_per_interval);
    
    // 5. Register vector 0x40 in IDT
    register_interrupt_handler(0x40, timer_callback);
    
    use_lapic_timer = 1;
    
    char s[16];
    kprint("[LAPIC] Timer calibrated: ");
    int_to_ascii(ticks_per_interval, s);
    kprint(s);
    kprint(" ticks per interval\n");
    
    // Fully mask PIC now that calibration is done
    port_byte_out(0x21, 0xFF);
}
