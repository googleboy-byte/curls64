#include "task.h"
#include <cpu_local.h>
#include "kernel.h"
#include "pipe.h"
#include "../../libc/mem.h"
#include "../cpu/paging.h"
#include "../../libc/string.h"
#include "../cpu/gdt.h"
#include "syscall_dispatch.h"
#include "../ktrace/ktrace.h"

#include "../../include/kabi/kabi_v1.h"
#include "signal.h"

// The start of the task linked list.
volatile task_t *ready_queue;

/* The active scheduler policy */
kabi_scheduler_ops_t *current_scheduler = 0;

/* Sorted sleep queue: tasks ordered by ascending sleep_until */
volatile task_t *sleep_queue = 0;

#include <spinlock.h>
static spinlock_t pid_lock = SPINLOCK_INIT;
static spinlock_t rq_lock = SPINLOCK_INIT;
static spinlock_t sq_lock = SPINLOCK_INIT;

void kabi_scheduler_register(kabi_scheduler_ops_t *ops) {
    if (!ops) return;
    current_scheduler = ops;
    kprint("[SCHED] Registered: ");
    kprint((char*)ops->name);
    kprint("\n");
}

void set_scheduler(scheduler_ops_t *sched) {
    kabi_scheduler_register(sched);
}

/* ============================================================
 * Sleep Queue: sorted singly-linked list by wake tick
 * ============================================================ */

void sleepq_insert(task_t *t, uint32_t wake_tick) {
    t->sleep_until = wake_tick;
    t->sleep_next  = NULL;

    spin_lock(&sq_lock);
    /* Insert sorted by wake_tick (ascending) */
    if (!sleep_queue || wake_tick <= sleep_queue->sleep_until) {
        t->sleep_next = (task_t*)sleep_queue;
        sleep_queue = t;
        spin_unlock(&sq_lock);
        return;
    }
    task_t *prev = (task_t*)sleep_queue;
    while (prev->sleep_next && prev->sleep_next->sleep_until <= wake_tick) {
        prev = prev->sleep_next;
    }
    t->sleep_next = prev->sleep_next;
    prev->sleep_next = t;
    spin_unlock(&sq_lock);
}

void sleepq_remove(task_t *t) {
    if (!t->sleep_until) return; /* not sleeping */
    
    spin_lock(&sq_lock);
    t->sleep_until = 0;

    if ((task_t*)sleep_queue == t) {
        sleep_queue = t->sleep_next;
        t->sleep_next = NULL;
        spin_unlock(&sq_lock);
        return;
    }
    task_t *prev = (task_t*)sleep_queue;
    while (prev && prev->sleep_next != t) prev = prev->sleep_next;
    if (prev) prev->sleep_next = t->sleep_next;
    t->sleep_next = NULL;
    spin_unlock(&sq_lock);
}

// Some externs are needed to manipulate the kernel stack and page directory
extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;

uint32_t next_pid = 1;

// Global to communicate new ESP/RSP to the IRQ/ISR handlers
volatile uint32_t task_switch_esp = 0;

static void validate_task(task_t *t) {
    if (!t) panic("validate_task: NULL task");
    if (t->magic != TASK_MAGIC) {
        kprint("BAD MAGIC: "); char s[16]; int_to_ascii(t->magic, s); kprint(s); kprint("\n");
        // even though the above wont be visible without our current
        // panic implementation, still, lets keep it
        panic("validate_task: Task magic corrupted");
    }
    if (!t->kernel_stack) panic("validate_task: Task kernel_stack NULL");
    if (!t->user_esp) panic("validate_task: Task user_esp NULL");

    // Stack overflow detection
    uintptr_t *guard = (uintptr_t*)t->kernel_stack_base;
    if (*guard != STACK_MAGIC) {
        kprint("STACK OVERFLOW on PID "); char s[32], s2[32]; int_to_ascii(t->id, s); kprint(s);
        kprint(" (Expected: "); hex64_to_ascii(STACK_MAGIC, s); kprint(s);
        kprint(", Found: "); hex64_to_ascii(*guard, s); kprint(s);
        kprint(")\n");

        kprint("Memory dump at guard:\n");
        for (int i = 0; i < 8; i++) {
            hex64_to_ascii(guard[i], s);
            kprint("  +"); int_to_ascii(i*8, s2); kprint(s2); kprint(": "); kprint(s); kprint("\n");
        }

        panic("KERNEL STACK OVERFLOW");
    }
}

void check_pid2_guard(const char *label) {
    task_t *t = (task_t*)ready_queue;
    if (!t) return;
    task_t *start = t;
    do {
        if (t->id == 2 && t->kernel_stack_base) {
            uintptr_t *guard = (uintptr_t*)t->kernel_stack_base;
            if (*guard != STACK_MAGIC) {
                char s[32];
                kprint("[GUARD CHECK] *** PID 2 CORRUPTED at checkpoint: ");
                kprint(label);
                kprint(" ***\n  Guard value: ");
                hex64_to_ascii(*guard, s); kprint(s);
                kprint("\n  Stack base: ");
                hex64_to_ascii(t->kernel_stack_base, s); kprint(s);
                kprint("\n");
                // Dump 8 words
                for (int i = 0; i < 8; i++) {
                    hex64_to_ascii(guard[i], s);
                    char s2[16]; int_to_ascii(i*8, s2);
                    kprint("  +"); kprint(s2); kprint(": "); kprint(s); kprint("\n");
                }
                panic("PID 2 guard corrupted (see checkpoint above)");
            }
            return;
        }
        t = t->next;
    } while (t && t != start);
}

void move_stack(void *new_stack_start, uint32_t size) {
    // Not used in the new model
    (void)new_stack_start;
    (void)size;
}


void idle_task(void) {
    for (;;) {
        reap_zombies();
        asm volatile("hlt");
    }
}

void assert_on_kstack(registers_t *regs) {
    if (!current_task) return;
    
    uintptr_t addr = (uintptr_t)regs;
    
    // Check if on task stack
    if (addr >= current_task->kernel_stack_base && addr < current_task->kernel_stack) {
        return;
    }
    
    // Check if on CPU stack (for user mode transitions)
    if (addr >= get_cpu_local()->kstack_base && addr < get_cpu_local()->kstack_top) {
        return;
    }

    panic("KERNEL STACK ESCAPE");
}

void init_tasking() {
    (void)irq_save();
    kprint("  - Initializing 'current_task' and 'ready_queue'...\n");

    // Allocate current_task to represent the kernel boot sequence (PID 1)
    current_task = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    memory_set((uint8_t*)current_task, 0, sizeof(task_t));
    current_task->state = TASK_RUNNING;
    current_task->page_directory = kernel_directory;
    current_task->magic = TASK_MAGIC;

    // CRITICAL: PID 1 is currently running on the boot stack.
    // Under legacy BIOS loader this is 0x90000; under GRUB/Multiboot2 it is the
    // explicit mb2_boot_stack in the kernel image.
    extern uint8_t mb2_boot_stack;
    extern uint8_t mb2_boot_stack_top;
    uintptr_t esp;
#ifdef ARCH_X86_64
    asm volatile("mov %%rsp, %0" : "=r"(esp));
#else
    asm volatile("mov %%esp, %0" : "=r"(esp));
#endif

    uintptr_t mb2_base = (uintptr_t)&mb2_boot_stack;
    uintptr_t mb2_top  = (uintptr_t)&mb2_boot_stack_top;

    if (esp >= mb2_base && esp <= mb2_top) {
        current_task->kernel_stack_base = mb2_base;
        current_task->kernel_stack      = mb2_top;
        current_task->user_esp          = mb2_top;
    } else {
        current_task->kernel_stack_base = 0x80000;
        current_task->kernel_stack      = 0x90000;
        current_task->user_esp          = 0x90000;
    }

    // Poison the bottom of PID 1 stack as a guard
    *(uintptr_t*)current_task->kernel_stack_base = STACK_MAGIC;

    // Now it's safe if interrupts are enabled by spin_unlock
    current_task->capabilities = CAP_REBOOT | CAP_SHUTDOWN | CAP_SYS_ADMIN;
    strcpy((char*)current_task->cwd, "/");

    spin_lock(&pid_lock);
    current_task->id = next_pid++;
    ready_queue = current_task;
    
    // Set TSS for PID 1 before releasing the lock and enabling interrupts
    set_kernel_stack(current_task->kernel_stack);
    spin_unlock(&pid_lock);

    if (kabi_debug_enabled()) {
        char s[20];
        kprint("[SCHED] PID 1 task struct at 0x"); hex64_to_ascii((uint64_t)current_task, s); kprint(s); kprint("\n");
    }

    // Create the idle task
    task_t *idle = create_kernel_task(idle_task);
    
    char s_base[20], s_top[20];
    hex64_to_ascii(idle->kernel_stack_base, s_base);
    hex64_to_ascii(idle->kernel_stack, s_top);
    kprint("[BOOT] Idle Task (PID 2) Stack: "); kprint(s_base); kprint(" - "); kprint(s_top); kprint("\n");

    // Link circularly: boot -> idle -> boot
    current_task->next = idle;
    idle->next = (task_t*)ready_queue;

    kprint("  - Tasking initialized (Boot context preserved).\n");
    
    // Initialize standard FDs for PID 1
    extern void vfs_init_standard_fds(void *task_ptr);
    vfs_init_standard_fds((void*)current_task);

    // NOTE: Default scheduler will be registered via K-ABI in kernel_main
}



task_t *create_kernel_task(void (*entry)(void)){
    task_t *new_task = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    if (!new_task) panic("create_kernel_task: Out of memory for task_t");
    memory_set((uint8_t*)new_task, 0, sizeof(task_t));
    spin_lock(&pid_lock);
    new_task->id = next_pid++;
    spin_unlock(&pid_lock);
    new_task->page_directory = kernel_directory;
    new_task->state = TASK_READY;
    new_task->capabilities = CAP_NONE;
    new_task->magic = TASK_MAGIC;

    // Allocate kernel stack (16KB)
    phys_addr_t phys;
    virt_addr_t base = (virt_addr_t)kmalloc(0x4000, 1, &phys);
    if (!base) panic("create_kernel_task: Out of memory for kernel stack");
    
    // Poison stack for overflow detection
    memory_set((uint8_t*)base, 0xCC, 0x4000);
    *(uintptr_t*)base = STACK_MAGIC;
    
    new_task->kernel_stack_base = base;
    new_task->kernel_stack = base + 0x4000; // top

    uint64_t *stack = (uint64_t*)new_task->kernel_stack;

    // CPU-pushed (iret frame) — RING 0 -> RING 0
    *(--stack) = 0x10;                 // SS
    *(--stack) = (uint64_t)stack + 8;  // RSP (dummy for now)
    *(--stack) = 0x202;                // RFLAGS (IF=1)
    *(--stack) = 0x08;                 // CS (kernel code)
    *(--stack) = (uint64_t)entry;      // RIP

    // ISR-pushed (err_code, int_no)
    *(--stack) = 0;                    // err_code
    *(--stack) = 32;                   // int_no (IRQ0 / Timer)

    // Push registers in the order interrupt64.asm POPS them (reverse push order):
    // ASM pushes: rax, rbx, rcx, rdx, rsi, rdi, rbp, r8-r15
    // So iretq frame on stack (top to bottom): r15, r14, ..., rax, int_no, err, rip...
    // We build it from top (high addr) downward:
    *(--stack) = 0; // rax  <- first pushed by asm, so lowest in stack (last here)
    *(--stack) = 0; // rbx
    *(--stack) = 0; // rcx
    *(--stack) = 0; // rdx
    *(--stack) = 0; // rsi
    *(--stack) = 0; // rdi
    *(--stack) = 0; // rbp
    *(--stack) = 0; // r8
    *(--stack) = 0; // r9
    *(--stack) = 0; // r10
    *(--stack) = 0; // r11
    *(--stack) = 0; // r12
    *(--stack) = 0; // r13
    *(--stack) = 0; // r14
    *(--stack) = 0; // r15  <- last pushed by asm = top of saved frame
    
    // matches interrupt64.asm pop gs, pop fs, pop es, pop ds
    *(--stack) = 0x10; // ds
    *(--stack) = 0x10; // es
    *(--stack) = 0x10; // fs
    *(--stack) = 0x10; // gs

    new_task->user_esp = (virt_addr_t)stack;
    
#ifdef ARCH_X86_64
    // On x86_64, link new task into the ready_queue circular list.
    uint32_t f = irq_save();
    spin_lock(&rq_lock);
    task_t *tail = (task_t*)ready_queue;
    while (tail->next && tail->next != ready_queue) tail = tail->next;
    new_task->next = (task_t*)ready_queue;
    tail->next = new_task;
    spin_unlock(&rq_lock);
    irq_restore(f);
#endif
    return new_task;
}

int sys_fork(registers_t *regs) {
    uintptr_t f = irq_save();
    task_t *parent = (task_t*)current_task;
    
    KTRACE0(KTRACE_TASK_CREATE);
    page_directory_t *directory = clone_page_directory(parent->page_directory);
    
    // CRITICAL FIX: clone_page_directory marked the user-space page tables as 
    // MMU_COW and cleared MMU_WRITABLE. However, the executing parent process
    // still has the WRITABLE entries cached in its TLB. We MUST flush the TLB 
    // now so that when the parent returns to user-space, its first stack write
    // triggers a COW fault. Otherwise, it will write directly to the shared 
    // physical page, corrupting the child's identical stack frame.
    mmu_switch((mmu_context_t *)parent->page_directory);

    // Phase 2: Create new task structure
    
    // adding a task limit here for forks even though
    // right now we can only afford 24 heh
    // since we alloc a kernel stack per task

    spin_lock(&pid_lock);
    if (next_pid > MAX_TASKS) {
        spin_unlock(&pid_lock);
        kprint("[SCHED] fork: MAX_TASKS reached\n");
        irq_restore(f);
        return -KABI_ENOMEM;
    }

    if (kabi_debug_enabled()) kprint("[FORK] pd cloned, alloc child... ");
    task_t *child = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    if (!child) { spin_unlock(&pid_lock); panic("sys_fork: Out of memory for task_t"); }
    if (kabi_debug_enabled()) kprint("OK ");
    memory_set((uint8_t*)child, 0, sizeof(task_t));
    child->id = next_pid++;
    spin_unlock(&pid_lock);
    child->page_directory = directory;
    child->parent = parent;
    child->state = TASK_READY;
    child->magic = TASK_MAGIC;
    strcpy(child->cwd, parent->cwd);

    // Allocate kernel stack for child
    phys_addr_t phys;
    virt_addr_t base = (virt_addr_t)kmalloc(0x4000, 1, &phys);
    if (!base) panic("sys_fork: Out of memory for kernel stack");
    
    // Poison stack for overflow detection
    memory_set((uint8_t*)base, 0xCC, 0x4000);
    *(uintptr_t*)base = STACK_MAGIC;
    
    child->kernel_stack_base = base;
    child->kernel_stack = base + 0x4000;

    // PHASE 3: Surgical Stack Cloning
    // Determine the source stack top (could be task's private stack or CPU entry stack)
    virt_addr_t src_stack_top = parent->kernel_stack;
    if ((virt_addr_t)regs >= cpu_local[0].kstack_base && (virt_addr_t)regs < cpu_local[0].kstack_top) {
        src_stack_top = cpu_local[0].kstack_top;
    }

    virt_addr_t stack_used = src_stack_top - (virt_addr_t)regs;
    
    // SAFETY: Prevent stack smashing if parent (sh) uses a massive stack
    if (stack_used > 0x4000) {
        panic("sys_fork: stack depth exceeds limit (16KB)");
    }

    memory_copy((uint8_t*)regs, (uint8_t*)(child->kernel_stack - stack_used), stack_used);

    // PHASE 4: Fix child register state
    int64_t stack_shift = (int64_t)child->kernel_stack - (int64_t)src_stack_top;
    child->user_esp = (virt_addr_t)(child->kernel_stack - stack_used);

    registers_t *child_regs = (registers_t*)child->user_esp;
    child_regs->rax = 0;             // Child returns 0
    // Only adjust RSP for kernel-mode forks (ring 0).
    // For user-mode forks (ring 3), regs->rsp is the user's stack pointer
    // (pushed by the CPU on int 0x80) and must NOT be shifted.
    if ((regs->cs & 0x3) == 0) {
        child_regs->rsp += stack_shift;
    }

    // PHASE 4.1: Parent-Relative EBP Chain Fixup
    virt_addr_t src_stack_base = parent->kernel_stack_base;
    if (src_stack_top == cpu_local[0].kstack_top) src_stack_base = cpu_local[0].kstack_base;

    if (regs->rbp >= src_stack_base && regs->rbp < src_stack_top) {
        virt_addr_t parent_rbp = regs->rbp;
        virt_addr_t child_rbp  = parent_rbp + stack_shift;
        child_regs->rbp = child_rbp;

        int ebp_depth = 0;
        while (parent_rbp >= src_stack_base && parent_rbp < src_stack_top) {
            if (++ebp_depth > 64) panic("EBP LOOP TOO DEEP");
            virt_addr_t next_parent_rbp = *(virt_addr_t*)parent_rbp;
            if (next_parent_rbp < src_stack_base || next_parent_rbp >= src_stack_top)
                break;

            virt_addr_t next_child_rbp = next_parent_rbp + stack_shift;
            *(virt_addr_t*)(child_rbp) = next_child_rbp;

            parent_rbp = next_parent_rbp;
            child_rbp  = next_child_rbp;
        }
    } else {
        // User-mode EBP or garbage, do not shift
        child_regs->rbp = regs->rbp;
    }

    // Phase 5: File descriptor inheritance
    for (int i = 0; i < MAX_FD; i++) {
        if (current_task->fd_table[i]) {
            child->fd_table[i] = current_task->fd_table[i];
            child->fd_table[i]->refcount++;
            if (child->fd_table[i]->node->flags & FS_PIPE) {
                pipe_t *p = (pipe_t*)child->fd_table[i]->node->impl;
                if (child->fd_table[i]->flags & O_WRONLY) pipe_add_writer(p);
                else pipe_add_reader(p);
            }
        }
    }

    // Phase 6: Scheduler integration (Circular List)
    spin_lock(&rq_lock);
    child->next = parent->next;
    parent->next = child;
    spin_unlock(&rq_lock);

    // Notify scheduler
    if (current_scheduler && current_scheduler->on_task_added) {
        current_scheduler->on_task_added(child);
    }

    KTRACE1(KTRACE_TASK_CREATE, child->id);
    irq_restore(f);
    return child->id; // Parent returns child PID
}

int fork() {
    int pid;
    asm volatile("mov %1, %%eax; int $0x80; mov %%eax, %0" : "=r"(pid) : "i"(UABI_FORK) : "eax");
    return pid;
}

// Create a new process that will run user-mode code at the given entry point
int spawn_process(virt_addr_t entry_point, virt_addr_t user_stack) {
    uint32_t f = irq_save();

    task_t *parent_task = (task_t*)current_task;
    
    // We MUST clone the kernel directory to get a private copy we can modify
    page_directory_t *directory = clone_page_directory(kernel_directory);

    if (kabi_debug_enabled()) {
        char s[20];
        kprint("[SCHED] PID 1 task struct at 0x"); hex64_to_ascii((uint64_t)current_task, s); kprint(s); kprint("\n");
    }
    
    // Create new task struct
    
    // set task limit wherever task_t created
    spin_lock(&pid_lock);
    if (next_pid > MAX_TASKS) {
        spin_unlock(&pid_lock);
        kprint("[SCHED] spawn: MAX_TASKS reached\n");
        irq_restore(f);
        return -1;
    }

    task_t *new_task = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    if (!new_task) { spin_unlock(&pid_lock); panic("spawn_process: Out of memory for task_t"); }
    memory_set((uint8_t*)new_task, 0, sizeof(task_t));
    new_task->id = next_pid++;
    spin_unlock(&pid_lock);
    new_task->page_directory = directory;
    new_task->parent = parent_task;
    new_task->state = TASK_READY;
    new_task->magic = TASK_MAGIC;
    new_task->next = 0;
    new_task->user_eip = 0;

    // Inherit FD table from parent
    int i;
    for (i = 0; i < MAX_FD; i++) {
        if (parent_task->fd_table[i]) {
            new_task->fd_table[i] = parent_task->fd_table[i];
            new_task->fd_table[i]->refcount++;

            // PIPE REFCOUNTING: Increment reader/writer counts on inheritance
            if (new_task->fd_table[i]->node->flags & FS_PIPE) {
                pipe_t *p = (pipe_t*)new_task->fd_table[i]->node->impl;
                if (new_task->fd_table[i]->flags & O_WRONLY) pipe_add_writer(p);
                else pipe_add_reader(p);
            }
        } else {
            new_task->fd_table[i] = 0;
        }
    }
    
    // Initialize CWD: inherit from parent or default to root
    if (parent_task) {
        strcpy(new_task->cwd, parent_task->cwd);
    } else {
        strcpy(new_task->cwd, "/");
    }
    
    // If not inheriting (or to ensure they exist), initialize standard FDs if they are missing
    // Actually, inheritance is done above. But for a "spawn" from kernel_main (like user mode boot),
    // we want to make sure they are set if parent didn't have them.
    extern void vfs_init_standard_fds(void *task_ptr);
    for (int j = 0; j < 3; j++) {
        if (!new_task->fd_table[j]) {
            vfs_init_standard_fds((void*)new_task);
            break; 
        }
    }

    // Allocate a new kernel stack for the child (16KB, page-aligned)
    phys_addr_t stack_phys;
    virt_addr_t stack_base = (virt_addr_t)kmalloc(0x4000, 1, &stack_phys);
    if (!stack_base) panic("spawn_process: Out of memory for kernel stack");
    
    // Poison stack for overflow detection
    memory_set((uint8_t*)stack_base, 0xCC, 0x4000);
    *(uintptr_t*)stack_base = STACK_MAGIC;

    new_task->kernel_stack_base = stack_base;
    new_task->kernel_stack = stack_base + 0x4000; // Stack top

    // CRITICAL: Promote the code buffer and the user stack to USER mappings
    // 1. Promote entry_point (which is likely the start of the buffer we kmalloc'd)
    // We don't have the length of the program here, but we can guess or ideally
    // it was passed. For now, since it was kmalloc'd in shell, it's roughly 
    // accessible. However, shell calls spawn_process(buffer, stack).
    // Let's assume entry_point is the start of the program buffer.
    promote_to_user_table(directory, entry_point, 0x1000); // 4KB minimum for code
    // 2. Promote user stack
    promote_to_user_table(directory, user_stack - 0x1000, 0x1000); // 4KB stack

    // Add to ready queue (Circular List)
    spin_lock(&rq_lock);
    new_task->next = current_task->next;
    current_task->next = new_task;
    spin_unlock(&rq_lock);

    // Notify scheduler
    if (current_scheduler && current_scheduler->on_task_added) {
        current_scheduler->on_task_added(new_task);
    }

    // Build a fake interrupt frame on the child's kernel stack
    // This frame will be "restored" by IRET when the task is scheduled
    // 
    // Stack layout (grows down):
    // [Higher addresses - stack_top]
    //   SS        (user data segment)
    //   ESP       (user stack pointer)
    //   EFLAGS    (with IF=1)
    //   CS        (user code segment)
    //   EIP       (entry point)
    //   err_code  (0)
    //   int_no    (0)
    //   EAX       (0)
    //   ECX       (0)
    //   EDX       (0)
    //   EBX       (0)
    //   ESP_dummy (ignored by popa)
    //   EBP       (0)
    //   ESI       (0)
    //   EDI       (0)
    //   DS        (user data segment)
    // [Lower addresses - esp points here]
    
    uint64_t *stack = (uint64_t*)(new_task->kernel_stack);
    
    // User mode IRET frame (pushed in reverse order since stack grows down)
    *(--stack) = 0x23;              // SS (user data segment 0x20 | RPL 3)
    *(--stack) = user_stack;        // RSP (user stack)
    *(--stack) = 0x202;             // RFLAGS (IF=1, bit 1 always 1)
    *(--stack) = 0x1B;              // CS (64-bit user code segment | RPL 3)
    *(--stack) = entry_point;       // RIP (where to start executing)
    
    // Interrupt number and error code
    *(--stack) = 0;                 // Error code
    *(--stack) = 0;                 // Interrupt number
    
    // push general purpose registers (matches registers_t and interrupt64.asm)
    *(--stack) = 0; // rax
    *(--stack) = 0; // rbx
    *(--stack) = 0; // rcx
    *(--stack) = 0; // rdx
    *(--stack) = 0; // rsi
    *(--stack) = 0; // rdi
    *(--stack) = 0; // rbp
    *(--stack) = 0; // r8
    *(--stack) = 0; // r9
    *(--stack) = 0; // r10
    *(--stack) = 0; // r11
    *(--stack) = 0; // r12
    *(--stack) = 0; // r13
    *(--stack) = 0; // r14
    *(--stack) = 0; // r15
    
    // Segment registers (matches interrupt64.asm pop gs, pop fs, pop rax=es, pop rax=ds)
    *(--stack) = 0x23; // ds
    *(--stack) = 0x23; // es
    *(--stack) = 0x23; // fs
    *(--stack) = 0x23; // gs
    
    // The task's ESP points to the top of this fake frame
    new_task->user_esp = (virt_addr_t)stack;
    
    irq_restore(f);
    return new_task->id;
}



int getpid() {
    return current_task->id;
}

void ps() {
    kprint("PID  Status\n");
    task_t *task = (task_t*)ready_queue;
    if (!task) return;

    task_t *start_task = task;
    do {
        char s[16];
        int_to_ascii(task->id, s);
        kprint(s);
        if (task->state == TASK_RUNNING) kprint("    RUNNING\n");
        else if (task->state == TASK_READY) kprint("    READY\n");
        else if (task->state == TASK_WAITING) kprint("    WAITING\n");
        else kprint("    ZOMBIE\n");
        task = task->next;
    } while (task != start_task && task != 0);
}

void kill(int pid) {
    uint32_t f = irq_save();

    task_t *task = (task_t*)ready_queue;
    if (!task) { irq_restore(f); return; }

    if (pid == 2) {
        kprint("Cannot kill kernel idle task!\n");
        irq_restore(f);
        return;
    }

    task_t *start_task = task;
    do {
        if (task->id == (uint32_t)pid) {
            task_deliver_signal(task, SIGKILL);
            irq_restore(f);
            return;
        }
        task = task->next;
    } while (task != start_task && task != 0);

    irq_restore(f);
    return;
}

/* ============================================================
 * Signal Delivery Infrastructure
 * ============================================================ */

/*
 * task_deliver_signal: core signal handler, called with IRQs disabled.
 *
 * SIGKILL  - unconditional: mark ZOMBIE, wake parent, send SIGCHLD.
 * SIGTERM  - if sigterm_handler set: queue bit for userspace delivery.
 *            if no handler: same as SIGKILL.
 * SIGINT   - same default policy as SIGTERM.
 * SIGCHLD  - set bit in parent's pending_signals only (no kill).
 */
void task_deliver_signal(struct task_struct *t, int sig) {
    if (!t) return;

    switch (sig) {
        case SIGKILL:
            /* Uncatchable. Immediate termination. */
            goto do_terminate;

        case SIGTERM:
        case SIGINT:
            /* If the task has registered a handler, queue the signal. */
            if (t->sigterm_handler != 0) {
                t->pending_signals |= SIG_BIT(sig);
                return; /* Delivered — handler will be called on next schedule */
            }
            /* No handler: fall through to terminate */
            goto do_terminate;

        case SIGCHLD:
            /* Kernel-internal only: notify parent, no default action. */
            if (t->parent) {
                t->parent->pending_signals |= SIG_BIT(SIGCHLD);
            }
            return;

        default:
            return; /* Unknown signal — ignore */
    }

do_terminate:
    if (t->state == TASK_ZOMBIE) return; /* Already dead */

    sleepq_remove(t); /* Unlink from sleep queue before death */
    t->state = TASK_ZOMBIE;
    if (kabi_debug_enabled()) {
        char s[16]; int_to_ascii(t->id, s);
        kprint("[SIG] PID "); kprint(s);
        kprint(sig == SIGKILL ? " SIGKILL" :
               sig == SIGTERM ? " SIGTERM" : " SIGINT");
        kprint(" -> ZOMBIE\n");
    }

    /* Send SIGCHLD to parent */
    if (t->parent) {
        t->parent->pending_signals |= SIG_BIT(SIGCHLD);
        /* Wake parent if it was waiting */
        if (t->parent->state == TASK_WAITING) {
            t->parent->state = TASK_READY;
        }
    }

    /* If we just killed the current task, yield away */
    if (t == (task_t*)current_task) {
        irq_depth = 0;
        while(1) {
            asm volatile("sti; hlt");
        }
    }
}

int task_send_signal(int pid, int sig) {
    if (pid == 2) {
        kprint("[SIG] Cannot signal kernel idle task\n");
        return -1;
    }

    uint32_t f = irq_save();
    spin_lock(&rq_lock);
    task_t *task = (task_t*)ready_queue;
    if (!task) { spin_unlock(&rq_lock); irq_restore(f); return -1; }

    task_t *start = task;
    do {
        if (task->id == (uint32_t)pid) {
            task_deliver_signal(task, sig);
            spin_unlock(&rq_lock);
            irq_restore(f);
            return 0;
        }
        task = task->next;
    } while (task != start && task != 0);

    spin_unlock(&rq_lock);
    irq_restore(f);
    return -1; /* PID not found */
}

void task_send_sigint_foreground(void) {
    uint32_t f = irq_save();
    spin_lock(&rq_lock);
    task_t *task = (task_t*)ready_queue;
    if (!task) { spin_unlock(&rq_lock); irq_restore(f); return; }

    task_t *start = task;
    do {
        /* Filter: Only send SIGINT to tasks that have a sigterm_handler (Ring 3) 
         * and are not the idle task or kernel initialization context. */
        if (task->id > 2 && task->sigterm_handler != 0) {
            task_deliver_signal(task, SIGINT);
        }
        task = task->next;
    } while (task != start && task != 0);
    spin_unlock(&rq_lock);
    irq_restore(f);
}

void reap_zombies() {
    uint32_t f = irq_save();
    
    if (!ready_queue) {
        irq_restore(f);
        return;
    }

    task_t *task = (task_t*)ready_queue;
    task_t *prev = 0;
    
    // Find previous of head
    task_t *it = (task_t*)ready_queue;
    while (it->next != (task_t*)ready_queue && it->next != 0) {
        it = it->next;
    }
    prev = it;

    task_t *start = (task_t*)ready_queue;
    int reaped = 0;
    
    do {
        // Reap if:
        // 1. It's a zombie AND we are the parent
        // 2. It's a zombie AND we are the kernel (PID 1) - orphan reaping
        int should_reap = (task->state == TASK_ZOMBIE) && 
                         (task->parent == (task_t*)current_task || current_task->id == 1 || current_task->id == 2);

        if (should_reap) {
            if (task->next == task) break; // Cannot reap the last remaining task

            sleepq_remove(task); /* Ensure not dangling in sleep queue */

            spin_lock(&rq_lock);
            // Remove from list
            prev->next = task->next;
            if (task == (task_t*)ready_queue) {
                ready_queue = task->next;
            }
            spin_unlock(&rq_lock);

            // Free resources
            kfree((void*)task->kernel_stack_base);
            
            extern void vfs_close_all_fds(void *task_ptr);
            vfs_close_all_fds(task);
            
            task_t *to_free = task;
            task = prev->next;

            // Notify scheduler
            if (current_scheduler && current_scheduler->on_task_removed) {
                current_scheduler->on_task_removed(to_free);
            }

            // Free paging resources (and release COW frames)
            if (to_free->page_directory && to_free->page_directory != kernel_directory) {
                free_page_directory(to_free->page_directory);
            }

            kfree(to_free);
            
            reaped++;
            // Update start if we just reaped the original head
            if (to_free == start) {
                start = task;
                if (!start) break;
            }
            
            // If we just reaped everything back to start, break
            if (task == start) break;
            
            continue; // Continue with next task from same prev
        }
        
        prev = task;
        task = task->next;
    } while (task != start && task != 0);

    irq_restore(f);
}

void kill_all_children() {
    uint32_t f = irq_save();
    task_t *task = (task_t*)ready_queue;
    if (!task) { irq_restore(f); return; }
    task_t *start = task;
    do {
        /* Exempt Kernel-Shell (PID 1) and Idle (PID 2) */
        if (task->id > 2) {
            task_deliver_signal(task, SIGKILL);
        }
        task = task->next;
    } while (task != start && task != 0);

    kprint("\n[Ctrl+C] All child processes marked for reaping.\n");
    irq_restore(f);
}

/* kill_foreground_processes: kept for compat, now delegates to task_send_sigint_foreground */
void kill_foreground_processes() {
    task_send_sigint_foreground();
}


void task_switch(registers_t *regs) {
    if (!ready_queue) panic("READY QUEUE NULL");

    // Consistency Check: current_task must be in ready_queue
    task_t *t = (task_t*)ready_queue;
    int seen_current = 0;
    int count = 0;
    do {
        validate_task(t);
        // guardrail
        if (++count > MAX_TASKS + 2) panic("READY QUEUE LOOP CORRUPTION");
        if (t == current_task) seen_current = 1;
        t = t->next;
    } while (t && t != ready_queue);

    if (!seen_current) panic("CURRENT TASK NOT IN READY QUEUE");

    if (irq_depth > 1) {
#ifdef KABI_DEBUG
        kprint("WARN: task_switch with high irq_depth=");
        char _ds[10]; int_to_ascii(irq_depth, _ds); kprint(_ds);
        kprint(" pid="); int_to_ascii(current_task->id, _ds); kprint(_ds);
        kprint("\n");
#endif
        return;
    }

    // Phase 1: Context Preservation for the outgoing task
    task_t *prev_task = (task_t*)current_task;
    
    // Save the current stack pointer (regs points to the frame on current stack)
    prev_task->user_esp = (virt_addr_t)regs;
    
    if (kabi_debug_enabled()) {
        char s[32];
        int_to_ascii(prev_task->id, s);
        kprint("[SCHED] Save: PID "); kprint(s); 
        kprint(" (REGS="); hex64_to_ascii((uint64_t)prev_task->user_esp, s); kprint(s); kprint(")\n");
    }

    if (prev_task->state == TASK_RUNNING) {
        prev_task->state = TASK_READY;
    }

    // Phase 2: Selection of the incoming task
    task_t *next_task = prev_task;

    if (current_scheduler && current_scheduler->pick_next) {
        kabi_task_t *out_task = NULL;
        if (current_scheduler->pick_next(&out_task) == KABI_SUCCESS) {
            next_task = (task_t*)out_task;
        }
    } else {
        // Fallback: simple round-robin walk
        task_t *t = (task_t*)prev_task->next;
        int rotations = 0;
        while (t && t != prev_task) {
            if (t->state == TASK_READY) { next_task = t; break; }
            t = t->next;
            if (++rotations > MAX_TASKS + 2) break;
        }
    }

    // If no other task is ready, just continue with the current one
    if (next_task == prev_task) {
        prev_task->state = TASK_RUNNING;
        return;
    }

    // Phase 3: Transition to the incoming task
    KTRACE2(KTRACE_SCHED_SWITCH, prev_task->id, next_task->id);

    current_task = next_task;
    validate_task((task_t*)current_task);
    current_task->state = TASK_RUNNING;

    if (kabi_debug_enabled()) {
        char s[32], s2[32], rip_s[32]; 
        int_to_ascii(prev_task->id, s); int_to_ascii(current_task->id, s2);
        
        kprint("[SCHED] Switch: PID "); kprint(s); kprint(" -> "); kprint(s2);
        kprint(" RESTORE="); hex64_to_ascii(current_task->user_esp,  s); kprint(s);
        kprint(" RIP="); hex64_to_ascii(current_task->user_eip, rip_s); kprint(rip_s);
        kprint(" CS="); int_to_ascii(current_task->page_directory ? 0x1B : 0x08, s); kprint(s);
        kprint("\n");
        // Dump the actual rax at the RESTORE pointer
        registers_t *dump_regs = (registers_t*)current_task->user_esp;
        kprint("  [DUMP] rax="); hex64_to_ascii(dump_regs->rax, s); kprint(s);
        kprint(" rip="); hex64_to_ascii(dump_regs->rip, s); kprint(s);
        kprint(" cs="); hex64_to_ascii(dump_regs->cs, s); kprint(s);
        kprint("\n");
    }

    // Update hardware context
    switch_page_directory(current_task->page_directory);
    set_kernel_stack(current_task->kernel_stack);

    // Final safety checks
    if ((current_task->user_esp < current_task->kernel_stack_base || 
         current_task->user_esp >= current_task->kernel_stack) &&
        (current_task->user_esp < cpu_local[0].kstack_base ||
         current_task->user_esp >= cpu_local[0].kstack_top)) {
        kprint("BAD ESP: 0x"); char s[16]; hex_to_ascii(current_task->user_esp, s); kprint(s); kprint("\n");
        panic("TASK SWITCH ESP OUT OF KSTACK");
    }
    if (current_task->user_esp & 7) panic("ESP NOT 64-BIT ALIGNED (Restore)");

    // Inform assembly stub of the new stack pointer
    extern volatile virt_addr_t task_switch_rsp;
    task_switch_rsp = current_task->user_esp;
    get_cpu_local()->_task_switch_rsp = task_switch_rsp;
}

void schedule(registers_t *regs) {
    if (!ready_queue) panic("READY QUEUE NULL (schedule)");
    if (irq_depth > 1) return; // Don't schedule in nested interrupts
    assert_on_kstack(regs);
    task_switch(regs);
}

int wait_for_children() {
    task_t *self = (task_t*)current_task;
    int last_status = 0;
    if (kabi_debug_enabled()) {
        char _s[20];
        kprint("WFC_ENTER: irq_depth="); int_to_ascii(irq_depth, _s); kprint(_s);
        kprint(" pid="); int_to_ascii(self->id, _s); kprint(_s);
        kprint("\n");
    }

    while (1) {
        uintptr_t f = irq_save();
        
        // maybe we have a race condition preventing shell from resuming
        // on task end??
        int active_children = 0;
        int zombies = 0;
        task_t *task = (task_t*)ready_queue;
        if (task) {
            task_t *start = task;
            do {
                if (task->parent == self) {
                    if (task->state == TASK_ZOMBIE) {
                        zombies++;
                        last_status = task->exit_code;
                    } else {
                        active_children++;
                    }
                }
                task = task->next;
            } while (task != start && task != 0);
        }

        if (active_children == 0 && zombies == 0) {
            if (kabi_debug_enabled()) kprint("[WAIT] No children left, returning.\n");
            irq_restore(f);
            if (kabi_debug_enabled()) { char _s[20]; kprint("WFC_RET0: irq_depth="); int_to_ascii(irq_depth, _s); kprint(_s); kprint("\n"); }
            return last_status;
        }

        if (zombies > 0) {
            irq_restore(f);
            reap_zombies();
            if (kabi_debug_enabled()) {
                char s[16]; int_to_ascii(last_status, s);
                kprint("[WAIT] Zombie reaped, returning status: "); kprint(s); kprint("\n");
            }
            if (kabi_debug_enabled()) { char _s[20]; kprint("WFC_RETZ: irq_depth="); int_to_ascii(irq_depth, _s); kprint(_s); kprint("\n"); }
            return last_status;
        }

        // Robust Wait: Set state under lock to prevent missing a wake-up
        self->state = TASK_WAITING;
        irq_restore(f);
        
        while (self->state == TASK_WAITING) {
            // Save and restore irq_depth across the blocking wait
            uint32_t saved_depth = irq_depth;
            irq_depth = 0;
            asm volatile("sti; hlt; cli");
            irq_depth = saved_depth;
        }
    }
}

/* ============================================================
 * Signal Trampoline: task_check_pending_signals
 * Called at the end of syscall_handler (before IRET).
 * ============================================================
 *
 * Trampoline bytes pushed onto the user stack:
 *   B8 32 00 00 00   mov eax, 50   ; UABI_SIGRETURN
 *   CD 80            int 0x80
 *
 * User stack layout after injection (stack grows down,
 * new ESP points at the return address slot):
 *
 *   [trampoline code: 7 bytes]  <- trampoline_ptr
 *   [sig_num  (arg via ESP+4)]  }
 *   [trampoline_ptr (ret addr)] } <- new ESP
 *
 * When the C handler executes "ret":
 *   -> jumps to trampoline_ptr
 *   -> trampoline: mov eax,50; int 0x80 (UABI_SIGRETURN)
 *   -> kernel restores saved_eip / saved_esp from task_t
 */
void task_check_pending_signals(registers_t *regs) {
    if (!current_task) return;
    task_t *t = (task_t*)current_task;

    /* Only when returning to Ring-3 (RPL in CS bits[1:0] == 3) */
    if ((regs->cs & 3) == 0) return;

    /* Reentrancy guard: never nest signal handlers */
    if (t->in_signal) return;

    /* Must have a registered handler */
    if (t->sigterm_handler == 0) return;

    /* SIGKILL is never a pending bit — it kills outright via task_deliver_signal.
     * Only SIGTERM and SIGINT reach pending_signals when a handler is registered. */
    int sig = 0;
    if (t->pending_signals & SIG_BIT(SIGTERM)) sig = SIGTERM;
    else if (t->pending_signals & SIG_BIT(SIGINT)) sig = SIGINT;
    if (sig == 0) return;

    /* Clear the bit — we are now dispatching it */
    t->pending_signals &= ~SIG_BIT(sig);

    /* Save original user context for sigreturn */
    t->saved_eip = regs->rip;
    t->saved_esp = regs->rsp;

    /* ---- Build signal frame on user stack ---- */

    /* Trampoline stub bytes: mov eax, 50; int 0x80 */
    static const uint8_t trampoline_code[7] = {
        0xB8, 50, 0x00, 0x00, 0x00,   /* mov eax, 50 (UABI_SIGRETURN) */
        0xCD, 0x80                     /* int 0x80                     */
    };

    virt_addr_t u = t->saved_esp;

    /* Push trampoline bytes (7 bytes grow into lower addresses) */
    u -= 7;
    uint8_t *tp = (uint8_t*)u;
    for (int i = 0; i < 7; i++) tp[i] = trampoline_code[i];
    virt_addr_t trampoline_ptr = u;

#ifdef ARCH_X86_64
    int is_32bit = ((regs->cs & 0xFFFF) == 0x2B);
#else
    int is_32bit = 1;
#endif

    if (is_32bit) {
        /* Align to 4-byte boundary */
        u &= ~(virt_addr_t)3;

        /* Push signal number (argument 1, accessible at ESP+4 in handler) */
        u -= 4;
        *(uint32_t*)u = (uint32_t)sig;

        /* Push return address (points to trampoline, accessible at ESP+0) */
        u -= 4;
        *(uint32_t*)u = (uint32_t)trampoline_ptr;
    } else {
        /* Align to 8-byte boundary */
        u &= ~(virt_addr_t)7;

        /* Push signal number (argument 1, accessible at RSP+8 in handler) */
        u -= 8;
        *(uint64_t*)u = (uint64_t)sig;

        /* Push return address (points to trampoline, accessible at RSP+0) */
        u -= 8;
        *(uint64_t*)u = (uint64_t)trampoline_ptr;
    }

    /* ---- Redirect IRET frame to handler ---- */
    if (is_32bit) {
        regs->rip = (uint32_t)t->sigterm_handler;
        regs->rsp = (uint32_t)u;
    } else {
        regs->rip = t->sigterm_handler;
        regs->rsp = u;
    }

    /* Mark task as inside signal handler (reentrancy guard) */
    t->in_signal = 1;

    if (kabi_debug_enabled()) {
        char s[16];
        kprint("[SIG] Dispatching ");
        kprint(sig == SIGTERM ? "SIGTERM" : "SIGINT");
        kprint(" -> handler 0x");
        hex_to_ascii(t->sigterm_handler, s); kprint(s);
        kprint(" newESP=0x");
        hex_to_ascii(u, s); kprint(s);
        kprint("\n");
    }
}
