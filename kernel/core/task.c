#include "task.h"
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

// The currently running task.
volatile task_t *current_task = 0;
volatile task_t *task_list = 0;
// The start of the task linked list.
volatile task_t *ready_queue;

/* The active scheduler policy */
kabi_scheduler_ops_t *current_scheduler = 0;

/* Sorted sleep queue: tasks ordered by ascending sleep_until */
volatile task_t *sleep_queue = 0;

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

    /* Insert sorted by wake_tick (ascending) */
    if (!sleep_queue || wake_tick <= sleep_queue->sleep_until) {
        t->sleep_next = (task_t*)sleep_queue;
        sleep_queue = t;
        return;
    }
    task_t *prev = (task_t*)sleep_queue;
    while (prev->sleep_next && prev->sleep_next->sleep_until <= wake_tick) {
        prev = prev->sleep_next;
    }
    t->sleep_next = prev->sleep_next;
    prev->sleep_next = t;
}

void sleepq_remove(task_t *t) {
    if (!t->sleep_until) return; /* not sleeping */
    t->sleep_until = 0;

    if ((task_t*)sleep_queue == t) {
        sleep_queue = t->sleep_next;
        t->sleep_next = NULL;
        return;
    }
    task_t *prev = (task_t*)sleep_queue;
    while (prev && prev->sleep_next != t) prev = prev->sleep_next;
    if (prev) prev->sleep_next = t->sleep_next;
    t->sleep_next = NULL;
}

// Some externs are needed to manipulate the kernel stack and page directory
extern page_directory_t *kernel_directory;
extern page_directory_t *current_directory;

uint32_t next_pid = 1;

// Global to communicate new ESP to the IRQ/ISR handlers
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
    uint32_t *guard = (uint32_t*)t->kernel_stack_base;
    if (*guard != STACK_MAGIC) {
        kprint("STACK OVERFLOW on PID "); char s[16]; int_to_ascii(t->id, s); kprint(s); kprint("\n");
        panic("KERNEL STACK OVERFLOW");
    }
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

void init_tasking() {
    asm volatile("cli");
    kprint("  - Initializing 'current_task' and 'ready_queue'...\n");

    // Allocate current_task to represent the kernel boot sequence (PID 1)
    current_task = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    memory_set((uint8_t*)current_task, 0, sizeof(task_t));
    current_task->id = next_pid++;
    current_task->state = TASK_RUNNING;
    current_task->page_directory = kernel_directory;
    current_task->magic = TASK_MAGIC;
    
    // CRITICAL: PID 1 is currently running on the boot stack.
    // Under legacy BIOS loader this is 0x90000; under GRUB/Multiboot2 it is the
    // explicit mb2_boot_stack in the kernel image.
    extern uint8_t mb2_boot_stack;
    extern uint8_t mb2_boot_stack_top;
    uint32_t esp;
    asm volatile("mov %%esp, %0" : "=r"(esp));

    uint32_t mb2_base = (uint32_t)&mb2_boot_stack;
    uint32_t mb2_top  = (uint32_t)&mb2_boot_stack_top;

    if (esp >= mb2_base && esp <= mb2_top) {
        current_task->kernel_stack_base = mb2_base;
        current_task->kernel_stack      = mb2_top;
        current_task->user_esp          = mb2_top; // prevent esp null panic on boot
    } else {
        // Legacy bootloader stack from boot/switch_pm.asm
        current_task->kernel_stack_base = 0x80000;
        current_task->kernel_stack      = 0x90000;
        current_task->user_esp          = 0x90000; // prevent esp null panic on boot
    }
    current_task->capabilities = CAP_REBOOT | CAP_SHUTDOWN | CAP_SYS_ADMIN;
    strcpy(current_task->cwd, "/");

    // Set TSS for PID 1 (though it's Ring 0, good for consistency)
    set_kernel_stack(current_task->kernel_stack);

    // Poison the bottom of PID 1 stack as a guard
    *(uint32_t*)current_task->kernel_stack_base = STACK_MAGIC;

    ready_queue = current_task;

    // Create background tasks
    task_t *idle = create_kernel_task(idle_task);

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
    new_task->id = next_pid++;
    new_task->page_directory = kernel_directory;
    new_task->state = TASK_READY;
    new_task->capabilities = CAP_NONE;
    new_task->magic = TASK_MAGIC;

    // Allocate kernel stack (16KB)
    uint32_t phys;
    uint32_t base = (uint32_t)kmalloc(0x4000, 1, &phys);
    if (!base) panic("create_kernel_task: Out of memory for kernel stack");
    
    // Poison stack for overflow detection
    memory_set((uint8_t*)base, 0xCC, 0x4000);
    
    new_task->kernel_stack_base = base;
    new_task->kernel_stack = base + 0x4000; // top

    uint32_t *stack = (uint32_t*)new_task->kernel_stack;

    // CPU-pushed (iret frame) — RING 0 -> RING 0
    *(--stack) = 0x202;                // EFLAGS (IF=1)
    *(--stack) = 0x08;                 // CS (kernel code)
    *(--stack) = (uint32_t)entry;      // EIP

    // ISR-pushed (err_code, int_no)
    *(--stack) = 0;                    // err_code
    *(--stack) = 32;                   // int_no (IRQ0 / Timer)

    // pusha (eax, ecx, edx, ebx, esp, ebp, esi, edi)
    *(--stack) = 0; // eax
    *(--stack) = 0; // ecx
    *(--stack) = 0; // edx
    *(--stack) = 0; // ebx
    *(--stack) = 0; // useless (esp placeholder)
    *(--stack) = 0; // ebp
    *(--stack) = 0; // esi
    *(--stack) = 0; // edi

    // ds
    *(--stack) = 0x10;

    new_task->user_esp = (uint32_t)stack;
    
    kprint("\n");
    
    return new_task;
}

int sys_fork(registers_t *regs) {
    uint32_t f = irq_save();
    task_t *parent = (task_t*)current_task;
    
    KTRACE0(KTRACE_TASK_CREATE);
    page_directory_t *directory = clone_page_directory(parent->page_directory);

    // Phase 2: Create new task structure
    
    // adding a task limit here for forks even though
    // right now we can only afford 24 heh
    // since we alloc a kernel stack per task
    if (next_pid > MAX_TASKS) {
        kprint("[SCHED] fork: MAX_TASKS reached\n");
        irq_restore(f);
        return -1;
    }

    if (kabi_debug_enabled()) kprint("[FORK] pd cloned, alloc child... ");
    task_t *child = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    if (!child) panic("sys_fork: Out of memory for task_t");
    if (kabi_debug_enabled()) kprint("OK ");
    memory_set((uint8_t*)child, 0, sizeof(task_t));
    child->id = next_pid++;
    child->page_directory = directory;
    child->parent = parent;
    child->state = TASK_READY;
    child->magic = TASK_MAGIC;
    strcpy(child->cwd, parent->cwd);

    // Allocate kernel stack for child
    uint32_t phys;
    uint32_t base = (uint32_t)kmalloc(0x4000, 1, &phys);
    if (!base) panic("sys_fork: Out of memory for kernel stack");
    
    // Poison stack for overflow detection
    memory_set((uint8_t*)base, 0xCC, 0x4000);
    
    child->kernel_stack_base = base;
    child->kernel_stack = base + 0x4000;

    // PHASE 3: Surgical Stack Cloning
    // Determine the source stack top (could be task's private stack or CPU entry stack)
    uint32_t src_stack_top = parent->kernel_stack;
    if ((uint32_t)regs >= cpu_local[0].kstack_base && (uint32_t)regs < cpu_local[0].kstack_top) {
        src_stack_top = cpu_local[0].kstack_top;
    }

    uint32_t stack_used = src_stack_top - (uint32_t)regs;
    
    // SAFETY: Prevent stack smashing if parent (sh) uses a massive stack
    if (stack_used > 0x4000) {
        panic("sys_fork: stack depth exceeds limit (16KB)");
    }

    memory_copy((uint8_t*)regs, (uint8_t*)(child->kernel_stack - stack_used), stack_used);

    // PHASE 4: Fix child register state
    int32_t stack_shift = (int32_t)child->kernel_stack - (int32_t)src_stack_top;
    child->user_esp = child->kernel_stack - stack_used;

    registers_t *child_regs = (registers_t*)child->user_esp;
    child_regs->eax = 0;             // Child returns 0

    // PHASE 4.1: Parent-Relative EBP Chain Fixup
    uint32_t src_stack_base = parent->kernel_stack_base;
    if (src_stack_top == cpu_local[0].kstack_top) src_stack_base = cpu_local[0].kstack_base;

    if (regs->ebp >= src_stack_base && regs->ebp < src_stack_top) {
        uint32_t parent_ebp = regs->ebp;
        uint32_t child_ebp  = parent_ebp + stack_shift;
        child_regs->ebp = child_ebp;

        int ebp_depth = 0;
        while (parent_ebp >= src_stack_base && parent_ebp < src_stack_top) {
            if (++ebp_depth > 64) panic("EBP LOOP TOO DEEP");
            uint32_t next_parent_ebp = *(uint32_t*)parent_ebp;
            if (next_parent_ebp < src_stack_base || next_parent_ebp >= src_stack_top)
                break;

            uint32_t next_child_ebp = next_parent_ebp + stack_shift;
            *(uint32_t*)(child_ebp) = next_child_ebp;

            parent_ebp = next_parent_ebp;
            child_ebp  = next_child_ebp;
        }
    } else {
        // User-mode EBP or garbage, do not shift
        child_regs->ebp = regs->ebp;
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
    child->next = parent->next;
    parent->next = child;

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
    asm volatile("mov %1, %%eax; int $0x80; mov %%eax, %0" : "=r"(pid) : "i"(SYS_FORK) : "eax");
    return pid;
}

// Create a new process that will run user-mode code at the given entry point
int spawn_process(uint32_t entry_point, uint32_t user_stack) {
    uint32_t f = irq_save();

    task_t *parent_task = (task_t*)current_task;
    
    // We MUST clone the kernel directory to get a private copy we can modify
    page_directory_t *directory = clone_page_directory(kernel_directory);

    // Create new task struct
    
    // set task limit wherever task_t created
    if (next_pid > MAX_TASKS) {
        kprint("[SCHED] spawn: MAX_TASKS reached\n");
        irq_restore(f);
        return -1;
    }

    task_t *new_task = (task_t*)kmalloc(sizeof(task_t), 0, 0);
    if (!new_task) panic("spawn_process: Out of memory for task_t");
    memory_set((uint8_t*)new_task, 0, sizeof(task_t));
    new_task->id = next_pid++;
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
    uint32_t stack_phys;
    uint32_t stack_base = (uint32_t)kmalloc(0x4000, 1, &stack_phys);
    if (!stack_base) panic("spawn_process: Out of memory for kernel stack");
    
    // Poison stack for overflow detection
    memory_set((uint8_t*)stack_base, 0xCC, 0x4000);

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
    new_task->next = current_task->next;
    current_task->next = new_task;

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
    
    uint32_t *stack = (uint32_t*)(new_task->kernel_stack);
    
    // User mode IRET frame (pushed in reverse order since stack grows down)
    *(--stack) = 0x23;              // SS (user data segment 0x20 | RPL 3)
    *(--stack) = user_stack;        // ESP (user stack)
    *(--stack) = 0x202;             // EFLAGS (IF=1, bit 1 always 1)
    *(--stack) = 0x1B;              // CS (user code segment 0x18 | RPL 3)
    *(--stack) = entry_point;       // EIP (where to start executing)
    
    // Interrupt number and error code
    *(--stack) = 0;                 // Error code
    *(--stack) = 0;                 // Interrupt number
    
    // General purpose registers (as pushed by pusha)
    *(--stack) = 0;                 // EAX
    *(--stack) = 0;                 // ECX
    *(--stack) = 0;                 // EDX
    *(--stack) = 0;                 // EBX
    *(--stack) = 0;                 // ESP (ignored by popa)
    *(--stack) = 0;                 // EBP
    *(--stack) = 0;                 // ESI
    *(--stack) = 0;                 // EDI
    
    // Data segment
    *(--stack) = 0x23;              // DS (user data segment)
    
    // The task's ESP points to the top of this fake frame
    new_task->user_esp = (uint32_t)stack;
    
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

    if (pid == 1) {
        kprint("Cannot kill kernel process!\n");
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
        if (irq_depth > 0) irq_depth--;
        while(1) {
            asm volatile("sti; hlt");
        }
    }
}

int task_send_signal(int pid, int sig) {
    if (pid == 1) {
        kprint("[SIG] Cannot signal kernel process\n");
        return -1;
    }

    uint32_t f = irq_save();
    task_t *task = (task_t*)ready_queue;
    if (!task) { irq_restore(f); return -1; }

    task_t *start = task;
    do {
        if (task->id == (uint32_t)pid) {
            task_deliver_signal(task, sig);
            irq_restore(f);
            return 0;
        }
        task = task->next;
    } while (task != start && task != 0);

    irq_restore(f);
    return -1; /* PID not found */
}

void task_send_sigint_foreground(void) {
    uint32_t f = irq_save();
    task_t *task = (task_t*)ready_queue;
    if (!task) { irq_restore(f); return; }

    task_t *start = task;
    do {
        /* Protect PID 1 (kernel shell), PID 2 (idle), PID 3 (user shell) */
        if (task->id > 3 && task->parent && task->parent->id > 2) {
            task_deliver_signal(task, SIGINT);
        }
        task = task->next;
    } while (task != start && task != 0);

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

            // Remove from list
            prev->next = task->next;
            if (task == (task_t*)ready_queue) {
                ready_queue = task->next;
            }

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
            if (to_free->page_directory) {
                free_page_directory(to_free->page_directory);
            }

            kfree(to_free);
            
            reaped++;
            // Update start if we just reaped the original head
            if (to_free == start) start = task;
            
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
        return; 
    }

    // Save the current stack pointer
    // alignment guardrail
    if (((uint32_t)regs) & 3) panic("ESP NOT WORD-ALIGNED (Save)");
    
    // If the task was interrupted on the per-CPU stack (User mode transition),
    // Save current task's registers
    // Since we now use per-task esp0 in TSS, regs is already on the task's private kernel stack.
    current_task->user_esp = (uint32_t)regs;
    
    // Mark current task as ready (it was running)
    if (current_task->state == TASK_RUNNING) {
        current_task->state = TASK_READY;
    }

    // Find the next READY task
    task_t *next_task = (task_t*)current_task;

    if (current_scheduler && current_scheduler->pick_next) {
        kabi_task_t *out_task = NULL;
        if (current_scheduler->pick_next(&out_task) == KABI_SUCCESS) {
            next_task = (task_t*)out_task;
        }
    } 

    if (next_task == current_task) return;

    // TRACE Transition
    // char s[10];
    // kprint("[SCHED] "); 
    // int_to_ascii(current_task->id, s); kprint(s);
    // kprint(" -> ");
    KTRACE2(KTRACE_SCHED_SWITCH, current_task->id, next_task->id);

    current_task = next_task;
    validate_task((task_t*)current_task);
    current_task->state = TASK_RUNNING;

    // Update TSS for the new task's kernel stack
    // Load TSS with the new task's kernel stack
    set_kernel_stack(current_task->kernel_stack);

    // Switch page directory
    current_directory = current_task->page_directory;
    uint32_t new_cr3 = current_directory->physicalAddr;
    asm volatile("mov %0, %%cr3" : : "r"(new_cr3));

    // Tell the IRQ handler to use this new stack
    if ((current_task->user_esp < current_task->kernel_stack_base || 
         current_task->user_esp >= current_task->kernel_stack) &&
        (current_task->user_esp < cpu_local[0].kstack_base ||
         current_task->user_esp >= cpu_local[0].kstack_top)) {
        kprint("BAD ESP: 0x"); char s[16]; hex_to_ascii(current_task->user_esp, s); kprint(s); kprint("\n");
        panic("TASK SWITCH ESP OUT OF KSTACK");
    }
    if (current_task->user_esp & 3) panic("ESP NOT WORD-ALIGNED (Restore)");
    task_switch_esp = current_task->user_esp;
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

    while (1) {
        uint32_t f = irq_save();
        
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
            irq_restore(f);
            return last_status;
        }

        if (zombies > 0) {
            if (kabi_debug_enabled()) {
                char s[16]; int_to_ascii(last_status, s);
                kprint("[WAIT] Zombie found, status="); kprint(s); kprint("\n");
            }
            irq_restore(f);
            reap_zombies();
            return last_status;
        }

        // Robust Wait: Set state under lock to prevent missing a wake-up
        self->state = TASK_WAITING;
        irq_restore(f);
        
        while (self->state == TASK_WAITING) {
            // Drop depth so timer interrupt can trigger a task switch
            if (irq_depth > 0) irq_depth--;
            asm volatile("sti; hlt; cli");
            irq_depth++;
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
    t->saved_eip = regs->eip;
    t->saved_esp = regs->esp;

    /* ---- Build signal frame on user stack ---- */

    /* Trampoline stub bytes: mov eax, 50; int 0x80 */
    static const uint8_t trampoline_code[7] = {
        0xB8, 50, 0x00, 0x00, 0x00,   /* mov eax, 50 (UABI_SIGRETURN) */
        0xCD, 0x80                     /* int 0x80                     */
    };

    uint32_t u = t->saved_esp;

    /* Push trampoline bytes (7 bytes grow into lower addresses) */
    u -= 7;
    uint8_t *tp = (uint8_t*)u;
    for (int i = 0; i < 7; i++) tp[i] = trampoline_code[i];
    uint32_t trampoline_ptr = u;

    /* Align to 4-byte boundary */
    u &= ~(uint32_t)3;

    /* Push signal number (argument 1, accessible at ESP+4 in handler) */
    u -= 4;
    *(uint32_t*)u = (uint32_t)sig;

    /* Push return address (points to trampoline, accessible at ESP+0) */
    u -= 4;
    *(uint32_t*)u = trampoline_ptr;

    /* ---- Redirect IRET frame to handler ---- */
    regs->eip = t->sigterm_handler;
    regs->esp = u;

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
