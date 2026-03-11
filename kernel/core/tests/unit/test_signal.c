/*
 * test_signal.c — Comprehensive Signal System Test Suite
 *
 * Covers:
 *   1. SIGKILL uncatchable (kills even with handler registered)
 *   2. SIGKILL overrides handler (pending bit not set)
 *   3. SIGTERM default kill (no handler registered)
 *   4. SIGCHLD delivery to parent on child exit
 *   5. Signal coalescing (multiple deliveries → one pending bit)
 *   6. Kernel-mode SIGKILL safe (kills a task that never reached Ring-3)
 *   7. Reentrancy guard (in_signal prevents nested handler dispatch)
 *   8. Trampoline IRET frame mutation (handler EIP/ESP set correctly)
 */

#include "test_signal.h"
#include "../../task.h"
#include "../../signal.h"
#include "../../../../include/kabi/kabi_v1.h"
#include "../../../../libc/string.h"
#include "../../../../libc/mem.h"

/* Shared flag for handler round-trip test */
static volatile int handler_was_called = 0;
static volatile int handler_sig_received = 0;

/* Fake handler function (kernel EIP — only used for frame-mutation test) */
static void fake_sig_handler(int sig) {
    handler_was_called = 1;
    handler_sig_received = sig;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void pass(const char *name) { kprint(name); kprint(": PASSED\n"); }
static void fail(const char *name, const char *reason) {
    kprint(name); kprint(": FAILED ("); kprint(reason); kprint(")\n");
}

static task_t *find_task(uint32_t pid) {
    extern volatile task_t *ready_queue;
    task_t *t = (task_t*)ready_queue;
    if (!t) return 0;
    task_t *start = t;
    do {
        if (t->id == pid) return t;
        t = t->next;
    } while (t != start && t != 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Test 1: SIGKILL is uncatchable — kills even if handler is registered */
/* ------------------------------------------------------------------ */
static void test_sigkill_uncatchable() {
    const char *name = "[SIG T1] SIGKILL uncatchable";
    int pid = kabi_fork();
    if (pid == 0) {
        /* Child: register handler, then busy-wait */
        current_task->sigterm_handler = (uint32_t)fake_sig_handler;
        for(;;) { asm volatile("hlt"); }
    }

    /* Parent: tiny delay, then SIGKILL */
    for(volatile int i = 0; i < 500000; i++);
    task_send_signal(pid, SIGKILL);

    /* Verify: child must be ZOMBIE regardless of handler */
    task_t *child = find_task((uint32_t)pid);
    if (child && child->state == TASK_ZOMBIE)
        pass(name);
    else
        fail(name, "child not zombie");

    reap_zombies();
}

/* ------------------------------------------------------------------ */
/* Test 2: SIGKILL does NOT set pending bit — handler ignored entirely */
/* ------------------------------------------------------------------ */
static void test_sigkill_no_pending_bit() {
    const char *name = "[SIG T2] SIGKILL ignores handler EIP";
    int pid = kabi_fork();
    if (pid == 0) {
        current_task->sigterm_handler = (uint32_t)fake_sig_handler;
        current_task->pending_signals = 0;
        for(;;) { asm volatile("hlt"); }
    }

    for(volatile int i = 0; i < 500000; i++);

    task_t *child = find_task((uint32_t)pid);
    if (!child) { fail(name, "child missing before kill"); return; }

    /* Deliver SIGKILL directly on the task struct */
    uint32_t f = irq_save();
    task_deliver_signal(child, SIGKILL);
    uint32_t bits = child->pending_signals; /* capture before reap */
    irq_restore(f);

    /* pending_signals must NOT have SIGKILL bit (9) set */
    if (child->state == TASK_ZOMBIE && !(bits & SIG_BIT(SIGKILL)))
        pass(name);
    else
        fail(name, "pending bit set or task not zombie");

    reap_zombies();
}

/* ------------------------------------------------------------------ */
/* Test 3: SIGTERM default action — no handler, must kill the task */
/* ------------------------------------------------------------------ */
static void test_sigterm_default() {
    const char *name = "[SIG T3] SIGTERM default kill";
    int pid = kabi_fork();
    if (pid == 0) {
        /* No handler registered */
        for(;;) { asm volatile("hlt"); }
    }

    for(volatile int i = 0; i < 500000; i++);
    task_send_signal(pid, SIGTERM);

    task_t *child = find_task((uint32_t)pid);
    if (child && child->state == TASK_ZOMBIE)
        pass(name);
    else
        fail(name, "child not zombie");

    reap_zombies();
}

/* ------------------------------------------------------------------ */
/* Test 4: SIGCHLD delivery — parent's pending bit set on child exit   */
/* ------------------------------------------------------------------ */
static void test_sigchld_delivery() {
    const char *name = "[SIG T4] SIGCHLD delivery";

    /* Clear parent's SIGCHLD bit */
    current_task->pending_signals &= ~SIG_BIT(SIGCHLD);

    int pid = kabi_fork();
    if (pid == 0) {
        /* Child exits immediately via SIGKILL */
        kill(getpid());
        while(1);
    }

    /* Wait for child to die */
    for(volatile int i = 0; i < 5000000; i++);

    if (current_task->pending_signals & SIG_BIT(SIGCHLD))
        pass(name);
    else
        fail(name, "SIGCHLD bit not set in parent");

    /* Clean up the bit and reap */
    current_task->pending_signals &= ~SIG_BIT(SIGCHLD);
    reap_zombies();
}

/* ------------------------------------------------------------------ */
/* Test 5: Signal coalescing — 5× SIGTERM → single pending bit        */
/* ------------------------------------------------------------------ */
static void test_signal_coalescing() {
    const char *name = "[SIG T5] Signal coalescing";
    int pid = kabi_fork();
    if (pid == 0) {
        /* Child: just busy-wait; parent sets the handler below */
        for(;;) { asm volatile("hlt"); }
    }

    for(volatile int i = 0; i < 500000; i++);

    task_t *child = find_task((uint32_t)pid);
    if (!child) { fail(name, "child missing"); return; }

    uint32_t f = irq_save();

    /* Set handler from parent side — no race condition with child scheduling */
    child->sigterm_handler = (uint32_t)fake_sig_handler;

    /* Deliver SIGTERM 5× — bitmask means only one bit can ever be set */
    for (int i = 0; i < 5; i++)
        task_deliver_signal(child, SIGTERM);

    uint32_t bits = child->pending_signals;
    irq_restore(f);

    /* Count set bits — must be exactly 1 (SIG_BIT(SIGTERM)) */
    uint32_t n = bits;
    int pop = 0;
    while (n) { pop += n & 1; n >>= 1; }

    if (pop == 1 && (bits & SIG_BIT(SIGTERM)))
        pass(name);
    else
        fail(name, "multiple bits or wrong bit");

    /* Cleanup: clear handler so SIGKILL kills rather than queues */
    f = irq_save();
    child->sigterm_handler = 0;
    child->pending_signals = 0;
    irq_restore(f);

    task_send_signal(pid, SIGKILL);
    reap_zombies();
}

/* ------------------------------------------------------------------ */
/* Test 6: SIGKILL from kernel mode — task never reached Ring-3        */
/* ------------------------------------------------------------------ */
static void test_sigkill_kernel_task() {
    const char *name = "[SIG T6] SIGKILL on kernel-mode task";

    /* create_kernel_task makes tasks that run in Ring 0 */
    extern task_t *create_kernel_task(void (*entry)(void));

    /* Tiny kernel task that loops */
    static void (*dummy_entry)(void) = 0;
    if (!dummy_entry) {
        /* anonymous: assign via lambda-like pattern */
        extern void idle_task(void);   /* idle_task is already Ring-0 */
        dummy_entry = idle_task;       /* borrow it as a stand-in */
    }

    task_t *kt = create_kernel_task(dummy_entry);
    if (!kt) { fail(name, "create_kernel_task returned null"); return; }

    /* Link it into the ready queue before signalling */
    uint32_t f = irq_save();
    kt->next = (task_t*)current_task->next;
    current_task->next = kt;
    irq_restore(f);

    /* SIGKILL the kernel task — it has cs=0x08 (Ring-0), state must go ZOMBIE */
    f = irq_save();
    task_deliver_signal(kt, SIGKILL);
    int is_zombie = (kt->state == TASK_ZOMBIE);
    irq_restore(f);

    if (is_zombie)
        pass(name);
    else
        fail(name, "kernel task not zombie");

    reap_zombies();
}

/* ------------------------------------------------------------------ */
/* Test 7: Reentrancy guard — in_signal blocks trampoline injection    */
/* ------------------------------------------------------------------ */
static void test_reentrancy_guard() {
    const char *name = "[SIG T7] Reentrancy guard";

    /* Set up a synthetic registers_t as if returning to Ring-3 */
    registers_t fake_regs;
    memory_set((uint8_t*)&fake_regs, 0, sizeof(fake_regs));
    fake_regs.cs      = 0x1B;          /* Ring-3 code segment */
    fake_regs.eip     = 0xDEAD0000;    /* fake user EIP */
    fake_regs.esp     = 0xBEEF0000;    /* fake user ESP */
    fake_regs.eflags  = 0x202;

    task_t *t = (task_t*)current_task;
    uint32_t saved_handler = t->sigterm_handler;
    uint32_t saved_pending = t->pending_signals;
    int      saved_in_sig  = t->in_signal;

    /* Set up: handler registered, SIGTERM pending, already inside handler */
    t->sigterm_handler = (uint32_t)fake_sig_handler;
    t->pending_signals = SIG_BIT(SIGTERM);
    t->in_signal       = 1;           /* ← reentrancy guard */

    uint32_t eip_before = fake_regs.eip;
    task_check_pending_signals(&fake_regs);

    int guarded = (fake_regs.eip == eip_before); /* EIP must be unchanged */

    /* Restore task state */
    t->sigterm_handler = saved_handler;
    t->pending_signals = saved_pending;
    t->in_signal       = saved_in_sig;

    if (guarded)
        pass(name);
    else
        fail(name, "IRET frame mutated despite in_signal=1");
}

/* ------------------------------------------------------------------ */
/* Test 8: Trampoline frame mutation — EIP redirected to handler       */
/* ------------------------------------------------------------------ */
static void test_handler_frame_mutation() {
    const char *name = "[SIG T8] Trampoline IRET frame mutation";

    /*
     * We need a writable "user stack" for the trampoline to write into.
     * Allocate a small kernel buffer and pretend it's the user stack top.
     * This works because task_check_pending_signals does a direct pointer
     * write — it doesn't care about page table flags here.
     */
    uint8_t fake_user_stack[64];
    memory_set(fake_user_stack, 0, sizeof(fake_user_stack));
    /* Point esp at the top of the buffer (stack grows down) */
    uint32_t fake_user_esp = (uint32_t)(fake_user_stack + sizeof(fake_user_stack));

    registers_t fake_regs;
    memory_set((uint8_t*)&fake_regs, 0, sizeof(fake_regs));
    fake_regs.cs     = 0x1B;         /* Ring-3 */
    fake_regs.eip    = 0xCAFEBABE;   /* original user EIP */
    fake_regs.esp    = fake_user_esp; /* original user ESP */
    fake_regs.eflags = 0x202;

    task_t *t = (task_t*)current_task;
    uint32_t saved_handler = t->sigterm_handler;
    uint32_t saved_pending = t->pending_signals;
    int      saved_in_sig  = t->in_signal;

    t->sigterm_handler = (uint32_t)fake_sig_handler;
    t->pending_signals = SIG_BIT(SIGTERM);
    t->in_signal       = 0;

    task_check_pending_signals(&fake_regs);

    /* ---- Verify the mutations ---- */
    int ok = 1;

    /* EIP must now be the handler, not the original EIP */
    if (fake_regs.eip != (uint32_t)fake_sig_handler) {
        fail(name, "EIP not redirected to handler");
        ok = 0;
    }

    /* SIGTERM bit must be cleared from pending_signals */
    if (t->pending_signals & SIG_BIT(SIGTERM)) {
        fail(name, "SIGTERM bit still set after dispatch");
        ok = 0;
    }

    /* in_signal must be set to 1 */
    if (!t->in_signal) {
        fail(name, "in_signal not set");
        ok = 0;
    }

    /* saved_eip must be the original EIP */
    if (t->saved_eip != 0xCAFEBABE) {
        fail(name, "saved_eip wrong");
        ok = 0;
    }

    /* saved_esp must be the original user ESP */
    if (t->saved_esp != fake_user_esp) {
        fail(name, "saved_esp wrong");
        ok = 0;
    }

    /* The new ESP must be below the original ESP (stack built downward) */
    if (fake_regs.esp >= fake_user_esp) {
        fail(name, "new ESP not below original");
        ok = 0;
    }

    /* [new_esp+0] must be the trampoline address (the return address) */
    uint32_t ret_addr = *(uint32_t*)fake_regs.esp;
    /* The trampoline starts somewhere below fake_user_esp */
    if (ret_addr < (uint32_t)fake_user_stack ||
        ret_addr >= (uint32_t)(fake_user_stack + sizeof(fake_user_stack))) {
        fail(name, "return address out of fake stack range");
        ok = 0;
    }

    /* [new_esp+4] must be the signal number (SIGTERM=15) */
    uint32_t sig_arg = *(uint32_t*)(fake_regs.esp + 4);
    if (sig_arg != (uint32_t)SIGTERM) {
        fail(name, "sig_num arg wrong");
        ok = 0;
    }

    /* The 7 trampoline bytes at ret_addr must be: B8 32 00 00 00 CD 80 */
    uint8_t *tramp = (uint8_t*)ret_addr;
    static const uint8_t expected[7] = {0xB8, 50, 0x00, 0x00, 0x00, 0xCD, 0x80};
    for (int i = 0; i < 7; i++) {
        if (tramp[i] != expected[i]) {
            fail(name, "trampoline bytes wrong");
            ok = 0;
            break;
        }
    }

    if (ok) pass(name);

    /* Restore task state */
    t->sigterm_handler = saved_handler;
    t->pending_signals = saved_pending;
    t->in_signal       = saved_in_sig;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */
void run_signal_tests() {
    kprint("\n=== Signal System Test Suite ===\n");
    test_sigkill_uncatchable();
    test_sigkill_no_pending_bit();
    test_sigterm_default();
    test_sigchld_delivery();
    test_signal_coalescing();
    test_sigkill_kernel_task();
    test_reentrancy_guard();
    test_handler_frame_mutation();
    kprint("=== Signal Tests Complete ===\n");
}
