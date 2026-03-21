#include "core_init.h"
#include "../cpu/isr.h"
#include "../cpu/paging.h"
#ifdef ARCH_X86_64
#include "../arch/x86_64/cpu/gdt.h"
#include "../arch/x86_64/acpi/acpi.h"
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
    init_tasking();
    init_syscalls();
#else
    init_tasking();
    init_syscalls();
#endif
    
    // Interrupts enabled by kernel_main or here?
    // User plan says 'asm volatile("sti")' at the end of core_init.
    irq_restore(0x202);
}
