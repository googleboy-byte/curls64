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

