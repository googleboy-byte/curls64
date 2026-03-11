#include "core_init.h"
#include "../cpu/isr.h"
#include "../cpu/paging.h"
#include "../cpu/gdt.h"
#include "task.h"
#include "syscall_dispatch.h"
#include "vfs_core.h"
#include "../ktrace/ktrace.h"
#include <stdint.h>

#include "kabi_bridge.h"

void core_init() {
#ifndef ARCH_X86_64
    init_gdt();
    isr_install();
    irq_install();
#endif
    init_paging();
    
#ifndef ARCH_X86_64
    /* Initialize ktrace after paging */
    ktrace_init();
    
    cpu_init(0);
    init_fs();
    kabi_bridge_init();
    init_tasking();
    init_syscalls();
#else
    // Minimal 64-bit bridge init if needed, for now just kprint
    kprint("64-bit Core Init: Paging and PMM Ready.\n");
#endif

#ifndef ARCH_X86_64
    // Interrupts enabled by kernel_main or here?
    // User plan says 'asm volatile("sti")' at the end of core_init.
    asm volatile("sti");
#endif
}
