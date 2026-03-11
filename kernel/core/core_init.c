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
    init_gdt();
    isr_install();
    irq_install();
    init_paging();
    
    /* Initialize ktrace after paging */
    ktrace_init();
    
    cpu_init(0);
    init_fs();
    kabi_bridge_init();
    init_tasking();
    init_syscalls();

    // Interrupts enabled by kernel_main or here?
    // User plan says 'asm volatile("sti")' at the end of core_init.
    asm volatile("sti");
}
