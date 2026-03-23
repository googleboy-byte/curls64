#ifndef TASK_H
#define TASK_H

#pragma once

#include "../cpu/paging.h"
#include <stdint.h>
#include "../cpu/isr.h"
#include "vfs_core.h" // Added for file_t and MAX_FD
#ifdef ARCH_X86_64
#include "../arch/x86_64/cpu/gdt.h"
#else
#include "../cpu/gdt.h"
#endif
#include "signal.h"

#define TASK_READY 0
#define TASK_RUNNING 1
#define TASK_WAITING 2
#define TASK_ZOMBIE 3
#define KERNEL_STACK_SIZE 0x2000
#define TASK_MAGIC 0xCAFEBABE
#ifdef ARCH_X86_64
#define STACK_MAGIC 0xCCCCCCCCCCCCCCCCULL
#else
#define STACK_MAGIC 0xCCCCCCCC
#endif
#define MAX_TASKS 128

// moving from proc owned kstack to exec context owned kstacks
#define MAX_CPU 1024


/* setting max tasks to 128 to prevent infinite tasking loop later */
/* when we improve arch to prevent one kernel stack per fork task */

/* The Process Control Block (PCB). 
 * The key insight: a task's context IS its stack pointer.
 * The stack contains the full saved interrupt frame.
 */
typedef struct task_struct {
    uint32_t magic;
    uint32_t id;                // Process ID.
    
    // DEPRECATED after introducing exec context based kstack allocation
    // since we must only user user esp and user eip now
    // uint32_t esp;               // Saved stack pointer (points to saved context)
    // uint32_t eip;		// newly added, not used, yet

    virt_addr_t user_esp;
    virt_addr_t user_eip;

    // DEPRACATED after introducing exec context based kstack allocation
    virt_addr_t kernel_stack;      // Top of kernel stack for this task (for TSS)
    virt_addr_t kernel_stack_base; // Base of kernel stack (for kfree)


    page_directory_t *page_directory; // Page directory.
    struct task_struct *parent;    // Parent task (for waiting)
    volatile uint8_t state;      // Current state.
    uint32_t capabilities;       // Process capabilities bitmask.
    file_t *fd_table[MAX_FD];    // File descriptor 
    char cwd[512];               // Current working directory
    struct task_struct *next;    // The next task in a linked list.

    /* --- Signal state --- */
    uint32_t pending_signals;    // Bitmask of pending signals (SIG_BIT(sig))
    uint32_t signal_mask;        // Bitmask of blocked signals (reserved, future)
    virt_addr_t sigterm_handler; // User-space EIP for SIGTERM/SIGINT handler (0 = default)
    virt_addr_t saved_eip;       // User EIP saved before signal handler dispatch (for sigreturn)
    virt_addr_t saved_esp;       // User ESP saved before signal handler dispatch (for sigreturn)
    int      in_signal;          // Reentrancy guard: non-zero while executing a signal handler
    uint32_t sleep_until;        // Tick to wake up on (0 = not sleeping)
    struct task_struct *sleep_next; // Next in sorted sleep queue (NULL = not queued)
    int      exit_code;          // Exit code for parent to reap
    uint32_t ticks;              // CPU ticks consumed by this task
} task_t;

typedef struct cpu_local{
    uint32_t id;
    
    // the kstack for this cpu
    virt_addr_t kstack_base;
    virt_addr_t kstack_top;

    // currently running task_t
    task_t *_current;

    // irq / nesting state
    uint32_t _irq_depth;
    volatile uint64_t _task_switch_rsp; // ADD THIS
    volatile uint64_t timer_ticks;
#ifdef ARCH_X86_64
    tss64_entry_t tss;
#else
    tss_entry_t tss;
#endif

    // for the future
    // per-cpu ktrace buffer
    // ktrace_buf_t ktrace;

} cpu_local_t;

/* Per-CPU context (defined in gdt.c) */
extern cpu_local_t cpu_local[];

/* Initializes the tasking system. */
void init_tasking();

/* Called by the timer hook, this changes the running process. */
void task_switch(registers_t *regs);


/* API */
task_t *create_kernel_task(void (*entry)(void));
void schedule(registers_t *r);

/* GLOBALS */
// scheduler ops
/* Forks the current process. */
int sys_fork(registers_t *regs);
int fork();

/* Create a new process to run user-mode code at the given entry point
 * with the given user stack. Returns the new process ID. */
int spawn_process(virt_addr_t entry_point, virt_addr_t user_stack);

/* Causes the current process' stack to be forcibly moved to a new location. */
void move_stack(void *new_stack_start, uint32_t size);

/* Jump to user mode with given entry point and stack */
extern void jump_to_user_mode(virt_addr_t address, virt_addr_t stack) __attribute__((noreturn));

/* Returns the pid of the current process. */
int getpid();

/* Prints a list of all current tasks. */
void ps();

/* Sleep queue API */
extern volatile task_t *sleep_queue;
void sleepq_insert(task_t *t, uint32_t wake_tick);
void sleepq_remove(task_t *t);

/* Kills a specific process. */
void kill(int pid);

/* Kills all child processes (PID > 1). */
void kill_all_children();
int wait_for_children();
void reap_zombies();

/* Signal-based kill helpers */
void task_deliver_signal(struct task_struct *t, int sig);
int  task_send_signal(int pid, int sig);
void task_send_sigint_foreground(void);

void panic(char *message);
void check_pid2_guard(const char *label);

#include "../../include/kabi/kabi_v1.h"

typedef kabi_scheduler_ops_t scheduler_ops_t;
extern kabi_scheduler_ops_t *current_scheduler;
void kabi_scheduler_register(kabi_scheduler_ops_t *ops);
void set_scheduler(scheduler_ops_t *sched);

/* Exec */
int sys_exec(const char *path);

extern uint32_t next_pid;

void assert_on_kstack(registers_t *regs);

static inline void assert_on_cpu_stack(cpu_local_t *cpu) {
    uintptr_t esp;
    asm volatile("mov %%rsp, %0" : "=r"(esp));
#ifdef ARCH_X86_64
    if (esp < cpu->kstack_base || esp >= cpu->kstack_top) {
#else
    if (esp < cpu->kstack_base || esp >= cpu->kstack_top) {
#endif
        panic("CPU STACK ESCAPE");
    }
}

#endif
