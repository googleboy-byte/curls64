# Diagnosis: SMP AP Stall — Kernel-Mode Fork with IF=0

**Date**: 2026-10-04  
**Session type**: Runtime verification + root-cause bisection  
**Affected area**: `kernel/core/task.c`, `kernel/cpu/timer.c`, `kernel/include/cpu_local.h`, `kernel/core/tests/`

---

## 1. Symptom

Under `-smp 4`, one AP stalled permanently at ~16 timer ticks. The victim CPU was non-deterministic (CPU 1, 2, or 3 across boots); the victim task was always PID 4 (the first child forked by the signal test suite). All locks were free at DIAG time. No kernel panic or triple fault — the rest of the system kept running.

---

## 2. Investigation

### 2.1 Instrumentation added

- `_stall_code` field in `cpu_local_t` — breadcrumb written at every early-return path in `task_switch`, `timer_callback`, and `task_deliver_signal` (later removed after root cause confirmed).
- DIAG command extended to decode all stall codes and display a per-CPU liveness probe (500ms window, delta >= 10 ticks expected per AP).
- Race detector in `reap_zombies`: checked `cpu_local[i]._current == to_free` before each `kfree`.

### 2.2 Hypotheses ruled out

| Hypothesis | Evidence against |
|---|---|
| H2: reap_zombies use-after-free on kernel stack | Race detector never fired. DIAG showed `_current = PID 4` on dead CPU — `cpu_id` was never cleared, so PID 4 was never reapable at the time of death. |
| rq_lock deadlock | rq_lock free at DIAG time. stall_code never reached 0xEE (spin on rq_lock). |
| sq_lock deadlock | sq_lock free. stall_code never reached 0x21. |
| LAPIC timer one-shot expiry | LVT configured periodic (0x20040). CPU 2 and 3 ticked normally. |
| Triple fault | BSP and other APs kept running — triple fault resets the whole VM. |

### 2.3 Key data point

`stall_code = 0x00` on the dead CPU. This is the value written immediately after `spin_lock(&rq_lock)` in Phase 2 of `task_switch`. The only way to leave that value as the last write is to complete Phase 3 (full context switch) and then never receive another timer interrupt.

### 2.4 Root cause confirmed

`[FORK] WARN: child PID N inherited IF=0` messages appeared for every kernel-mode fork (PIDs 4, 5, 6, 7, 8, 10). RFLAGS was `0x6` (IF bit 9 clear, only reserved bit 1 and PF bit 2 set).

**Mechanism**: `fork()` executes `int $0x80`. The IDT entry is an interrupt gate, which atomically clears IF in the saved RFLAGS pushed on the stack. `sys_fork` copied the parent's frame verbatim to the child stack without forcing IF=1. The child resumed with IF=0. When it hit `for(;;) { hlt; }`, the CPU halted permanently — no timer interrupt could wake it.

After forcing IF=1, all four CPUs showed delta >= 24 ticks over the 500ms probe window.

---

## 3. Fixes applied

### Fix 1: RFLAGS sanitization in `sys_fork` (task.c Phase 4.2)

```c
child_regs->rflags = (child_regs->rflags | 0x202) & ~(0x100 | 0x4000 | 0x3000);
```

- `0x202` = IF (bit 9) + reserved bit 1 (always 1 per Intel spec)
- `0x100` = TF (trap flag / single-step)
- `0x4000` = NT (nested task)
- `0x3000` = IOPL (always 0 for kernel children)
- Debug warn under `kabi_debug_enabled()` if IF was 0 before sanitization.

### Fix 2: H2 deferred `cpu_id` clear (task.c / task.h)

`task_switch` Phase 3 set `prev_task->cpu_id = -1` and released `rq_lock` while the CPU was still executing on `prev_task`'s kernel stack. `reap_zombies` could see `cpu_id == -1` and free the stack before the ISR stub completed.

Mitigation: `_previous_task` field added to `cpu_local_t`. Phase 2 (after `spin_lock`) clears the previous tick's `_previous_task->cpu_id`. Phase 3 sets `_previous_task = prev_task` instead of immediately clearing. Placement at Phase 2 entry ensures idle CPUs on the "same task" early-return path also drain `_previous_task` each tick.

> **Status**: H2 was not proven as the cause of this stall. Applied as defence-in-depth. To prove H2, poison the stack with `memset(..., 0xDD, 0x4000)` before `kfree` in `reap_zombies` and run with/without the fix.

---

## 4. Regression gates added (core_test64_v1.c — now 23 phases)

**Phase 22 — Fork RFLAGS**: Forks from kernel mode, waits 1s (50 BSP ticks via `sti;hlt;cli`), verifies the child exited (ZOMBIE or reaped). Child calls `int $0x80` with eax=32 (UABI_EXIT). Fails if child is still READY/RUNNING — indicates IF=0 stalled it.

**Phase 23 — AP liveness**: Waits 500ms (25 BSP ticks), checks every online AP shows delta >= 10 timer_ticks. Uses `ap_ready_flags` popcount for CPU count. Fails and reports which CPU stalled.

**`ASSERT_IF(tag)` macro** (`cpu_local.h`): `pushfq; pop; check bit 9; kprint if clear`. Placed between every signal test in `run_signal_tests()` to bisect which test leaves IF=0.

---

## 5. Open items

1. **IF=0 root cause not yet found**: The fork sanitization masks the symptom. Something in the test context runs with IF=0 for ~0.5s (BSP lost ~22 ticks during signal tests). ASSERT_IF probes will identify the exact test on next boot.

2. **H2 unproven**: Deferred clear is in place but race was never directly observed.

3. **Remaining audit items (priority order)**: H1 (atomic PMM frame alloc), H3 (rq_lock held across signal delivery), M7 (page-table refcount + H2 stack poison), H4 (tick/HZ constant), H6 (PID recycling), M9 (KILL/TERM tokenizer).
