#include "kernel.h"
#include "../cpu/ports.h"
#include "../../include/kabi/kabi_v1.h"

/**
 * @brief Formalized core reboot mechanism.
 * Invariant: No non-core code may directly trigger a CPU reset.
 */
void core_reboot(reboot_reason_t reason) {
    kprint("\n[CORE] System Reboot initiated. Reason: ");
    switch (reason) {
        case REBOOT_REASON_ADMIN:  kprint("Admin Request\n"); break;
        case REBOOT_REASON_PANIC:  kprint("Kernel Panic Recovery\n"); break;
        case REBOOT_REASON_UPDATE: kprint("System Update\n"); break;
        default: kprint("Unknown\n"); break;
    }

    // 1. Snapshot ktrace (if persistent ktrace is active, it survives reboot)
    // 2. Disable interrupts
    asm volatile("cli");

    // 3. Perform soft reboot / triple fault
    kprint("[CORE] Preparing for CPU Reset (Triple Fault)...\n");
    
    // Triple fault method: Load a zero-length IDT and trigger an interrupt
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idt_ptr = {0, 0};
    
    asm volatile("lidt %0" : : "m"(idt_ptr));
    asm volatile("int $3");

    // If we're still here, try the keyboard controller method
    while (port_byte_in(0x64) & 0x02);
    port_byte_out(0x64, 0xFE);

    // Final hang if all fails
    while(1) { asm volatile("hlt"); }
}

/**
 * @brief Formalized core shutdown mechanism.
 */
void core_shutdown() {
    kprint("\n[CORE] System Shutdown initiated.\n");
    
    asm volatile("cli");

    // QEMU/Bochs ACPI Power-off
    port_word_out(0x604, 0x2000);
    
    // Older Bochs/QEMU
    port_word_out(0xB004, 0x2000);
    
    // VirtualBox
    port_word_out(0x4004, 0x3400);

    kprint("[CORE] Shutdown failed. System Halted.\n");
    while(1) { asm volatile("hlt"); }
}
