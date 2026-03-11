#include "core_init.h"
#include "../cpu/isr.h"
#include "../cpu/paging.h"
#ifdef ARCH_X86_64
#include "../arch/x86_64/cpu/gdt.h"
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
#else
    init_gdt();
    isr_install();
    irq_install();
    init_paging();
#endif
    
#ifndef ARCH_X86_64
    /* Initialize ktrace after paging */
    ktrace_init();
#endif
    
    cpu_init(0);

#ifdef ARCH_X86_64
    kprint("64-bit Core Init: GDT, TSS, Paging and PMM Ready.\n");
    isr_install();
    irq_install();
    init_tasking();
    init_syscalls();
#else
    init_fs();
    kabi_bridge_init();
    init_tasking();
    init_syscalls();
#endif
    
    // Interrupts enabled by kernel_main or here?
    // User plan says 'asm volatile("sti")' at the end of core_init.
    asm volatile("sti");
}
