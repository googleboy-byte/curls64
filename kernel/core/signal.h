#ifndef SIGNAL_H
#define SIGNAL_H

#include <stdint.h>
#include "../cpu/isr.h"   /* for registers_t */

/* POSIX signal numbers — only the four we implement */
#define SIGINT  2   /* Interrupt (Ctrl+C) — default: terminate */
#define SIGKILL 9   /* Kill — cannot be caught or ignored */
#define SIGTERM 15  /* Terminate — can be caught via sigterm_handler */
#define SIGCHLD 17  /* Child stopped/exited — kernel-internal notification */

/* Bitmask helpers */
#define SIG_BIT(sig)  (1u << (sig))

/*
 * task_deliver_signal: Deliver a signal to a specific task struct.
 * Must be called with IRQs disabled or under irq_save().
 * Handles all four signals:
 *   SIGKILL  — unconditional ZOMBIE, wake parent
 *   SIGTERM  — if sigterm_handler != 0: queue bit; else ZOMBIE
 *   SIGINT   — same default action as SIGTERM (terminate)
 *   SIGCHLD  — set bit in parent->pending_signals, no default action
 */
struct task_struct;
void task_deliver_signal(struct task_struct *t, int sig);

/*
 * task_send_signal: Find task by PID and deliver signal.
 * Returns 0 on success, -1 if PID not found.
 * Handles IRQ save/restore internally.
 */
int task_send_signal(int pid, int sig);

/*
 * task_send_sigint_foreground: Send SIGINT to all foreground children.
 * Replaces kill_foreground_processes().
 */
void task_send_sigint_foreground(void);

/*
 * task_check_pending_signals: Called at the end of syscall_handler before IRET.
 * If the current task is returning to Ring-3 and has a pending signal with a
 * registered handler, modifies the IRET frame to redirect to the handler and
 * injects a sigreturn trampoline stub onto the user stack.
 *
 * Does nothing if:
 *   - Returning to kernel mode (cs & 3 == 0)
 *   - Task has no sigterm_handler registered
 *   - No handleable pending signals
 *   - Task is already executing a signal handler (in_signal != 0)
 */
void task_check_pending_signals(registers_t *regs);

#endif /* SIGNAL_H */
