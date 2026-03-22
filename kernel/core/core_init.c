#include "core_init.h"
#include "../cpu/isr.h"
#include "../cpu/paging.h"
#ifdef ARCH_X86_64
#include "../arch/x86_64/cpu/gdt.h"
#include "../arch/x86_64/acpi/acpi.h"
#include "../arch/x86_64/apic/lapic.h"
#include "../arch/x86_64/apic/ioapic.h"
#else
#include "../cpu/gdt.h"
#endif
#include "task.h"
#include "syscall_dispatch.h"
#include "vfs_core.h"
#include "../ktrace/ktrace.h"
#include <stdint.h>

#include "kabi_bridge.h"

void core_init() {
    kprint("Entering core_init()...\n");
#ifdef ARCH_X86_64
    init_paging();
    init_gdt();
    acpi_parse();
    lapic_init();
    ioapic_init();
    register_interrupt_handler(0xFF, lapic_spurious_handler);
#else
    init_gdt();
    isr_install();
    irq_install();
    init_paging();
#endif
    
    /* Initialize ktrace after paging */
    ktrace_init();
    
    cpu_init(0);

    kprint("  - Initializing VFS...\n");
    init_fs();
    kprint("  - Initializing K-ABI Bridge...\n");
    kabi_bridge_init();
    kprint("  - K-ABI Bridge ready.\n");

#ifdef ARCH_X86_64
    kprint("64-bit Core Init: GDT, TSS, Paging and PMM Ready.\n");
    isr_install();
    irq_install();
    irq_restore(0x202);

    init_lapic_timer();
    extern void smp_start_aps(void);
    smp_start_aps();
    
    init_tasking();
    extern void smp_signal_ready(void);
    smp_signal_ready();
    
    init_syscalls();
#else
    init_tasking();
    init_syscalls();
    irq_restore(0x202);
#endif
}
