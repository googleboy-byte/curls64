#include "core_init.h"
#include "kernel.h"
#include "../../include/kabi/kabi_v1.h"
#include "../ktrace/ktrace.h"
#include <stdint.h>
#include "boot_info.h"
#include "task.h"
#include "../../libc/string.h"

#ifdef ARCH_X86_64
#include "../arch/x86_64/cpu/gdt.h"
#else
#include "../cpu/gdt.h"
#endif

// Module initializers (declared here for simplicity, ideally in a module.h)
extern void sched_rr_init();
extern void fs_initrd_init();
extern void keyboard_driver_init();
extern void screen_driver_init();
extern void uart_init();
extern void uart_callback(void*);
extern void shell_init();
extern void sysmon_init();
extern void kernel_shell();
extern int  usb_msc_init();
extern int  usb_core_init();

// Output functions
void kprint(const char *c);

void kernel_main(void) {
    // 1. Initialize Sacred Core (Invariants)
    core_init();

#ifndef ARCH_X86_64
    // 2. Initialize Policy Modules via K-ABI
    // Order matters for some: screen first so we can see output
    screen_driver_init();
    keyboard_driver_init();
    uart_init();
    kabi_irq_register(4, uart_callback); // IRQ4: Serial COM1
    sched_rr_init();
    fs_initrd_init();

    // 3. Initialize USB stack
    // Register class drivers FIRST, then init core (which enumerates)
    usb_msc_init();     // Register mass storage class driver
    usb_core_init();    // EHCI probe → enumerate → match class drivers

    shell_init();
    sysmon_init();

    kprint("[BOOT] System composed. Transferring control to shell.\n");

    // 3. Start high-level orchestration
    kernel_shell();
#else
    kprint("[BOOT] 64-bit Core Foundation verified.\n");
    
    // Final verification of IDT/ISR in 64-bit mode
    void test_handler(registers_t *r) {
        kprint("  - [PHASE5] Success: IDT Handler Called for Interrupt 0x");
        char s[16]; hex_to_ascii(r->int_no, s); kprint(s); kprint("\n");
    }
    register_interrupt_handler(0x30, test_handler);

    kprint("[PHASE5] Testing IDT: Triggering software interrupt 0x30...\n");
    asm volatile("int $0x30");
    kprint("[PHASE5] IDT/ISR Test Completed.\n");
    kprint("[BOOT] x86_64 Kernel Halted after verification.\n");
#endif

    // 4. Idle loop
    while(1) {
        asm volatile("hlt");
    }
}

void panic(char *message) {
#ifndef ARCH_X86_64
    ktrace_panic_snapshot(message);
#endif

    // Emergency visual indication: prefer framebuffer if available, else VGA.
    if (boot_fb_info.present) {
#ifndef ARCH_X86_64
        extern void fb_clear(uint32_t rgb);
        fb_clear(0x000000);
#endif
    } else {
        volatile char *vga = (volatile char*)0xb8000;
        for(int i = 0; i < 80 * 25; i++) {
            vga[i*2] = ' ';
            vga[i*2+1] = 0x4F; // White on Red
        }
    }
}

void assert(int condition, char *message) {
    if (!condition) {
        panic(message);
    }
}
