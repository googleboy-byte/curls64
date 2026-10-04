# Postmortem

> Retrospective — what worked, what didn't, what we'd do differently.

---

## 2026-09-30 · Phase 3 Retrospective: SMP 4-Core Stabilization (`arch/smp`)

### What Worked Well:
1. **Systematic Execution Tracing**: Comparing execution traces and register dumps between `-smp 1` and `-smp 4` allowed us to pinpoint exact timing windows and isolate the `current_directory` TOCTOU race during Copy-On-Write page fault handling.
2. **Per-CPU Architecture Pattern (`cpu_local_t`)**: The `cpu_local_t` structure proved robust. Moving `current_directory` into per-CPU storage completely eliminated cross-core MMU races cleanly.
3. **Multi-Phase Automated Testing**: The 21-phase core test suite (`make run-grub64-verify-debug`) catch subtle memory and tasking regressions instantly.

### What Didn't / Difficulties:
1. **Hidden Global State from UP Era**: Legacy code originally written for single-core execution relied on global pointer variables (e.g. `current_directory`) that compiles cleanly but fails non-deterministically under multi-core execution.
2. **Busy-Wait Starvation**: Un-yielded process waiting loops (`wait_for_all_children`) consumed excessive CPU cycles on AP cores, starving child tasks trying to complete on other cores.

### What We'd Do Differently:
- Mandate a strict per-CPU variable audit whenever porting any subsystem from UP to SMP.
- Enforce CPU yielding (`hlt` or explicit scheduler yield) in all polling or waiting loops from day one.


---

## 2026-10-04 · AP Stall Bisection: SMP Runtime Verification

### What Worked Well:
1. **Diagnostic breadcrumb pattern**: `_stall_code` written at every early-return path in `task_switch` pinpointed the execution phase precisely (0x00 = completed Phase 3) without log spam.
2. **Liveness probe over fixed window**: Comparing `timer_ticks` before and after a 500ms wait was far more decisive than a single snapshot. A dead CPU has delta=0; a live CPU has delta~=25.
3. **User hypothesis over model hypothesis**: The IF=0 hypothesis came from careful reading of RFLAGS=0x6 and the interrupt-gate spec. The model's H2 (reap race) was plausible but wrong — the runtime race detector confirmed it never fired.
4. **Regression gates**: Phase 22 and 23 make this entire debugging session permanent. Any regression in fork RFLAGS or AP liveness will be caught at the next boot.

### What Didn't / Difficulties:
1. **`_stall_code` drift**: Having 20+ stall codes across 4 files was hard to maintain. Once the root cause was found, all codes were removed — they should have been built as a temporary probe from the start, not a permanent diagnostic.
2. **Phase 22 test bug**: The initial implementation used `int $0x80` with `eax=1` (legacy syscall), causing a PANIC instead of a clean exit. UABI_EXIT = 32 (defined in `include/uabi/uabi_v2.h`), not 1.

### What We'd Do Differently:
- Start bisection with `ASSERT_IF` at major checkpoints before adding per-phase stall codes — the assert macro would have identified IF=0 in one boot.
- Check UABI syscall numbers against `include/uabi/uabi_v2.h` before using them in test code.
- Add the AP liveness probe to the DIAG command from day one of SMP bring-up.
