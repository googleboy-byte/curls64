# Working Notes: current_directory SMP Race Analysis

**Date**: 2026-09-14

## Timeline of SMP introduction

| Commit | Date | What Changed |
|--------|------|-------------|
| `461f9d9` (phase8) | baseline | No SMP, no `-DSMP`, no `-smp` in QEMU. `current_directory` is global but safe on UP. |
| `53cbac3` | 2026-03-22 | ACPI parsing for SMP — no paging changes |
| `c27ace3` | 2026-03-22 | LAPIC support — no paging changes |
| `0f66ea4` | 2026-03-22 | **Initial SMP: AP trampoline, SIPI. QEMU gets `-smp 4` in debug target.** No `-DSMP` flag yet. |
| `dac4df9` | 2026-03-22 | GS base `cpu_local` — no paging changes |
| `395ccae` | 2026-03-22 | I/O APIC — no paging changes |
| `4c51338` | 2026-06-23 | **Big SMP integration.** mmu.c gets spinlocks for kernel PD. Still no `-DSMP`. |
| `6542f9d` | 2026-06-23 | **TLB shootdown. `-DSMP` flag added to CFLAGS64.** mmu.c gets `mmu_invlpg` SMP shootdown. |
| `2d7d9ba` | 2026-06-23 | CR3-aware TLB shootdown — mmu.c changes |
| `a02dd47` | 2026-06-23 | **`task_switch_rsp` moved from global to per-CPU** — the EXACT same class of bug, but fixed for a different variable. |
| `2a5e60d` | 2026-06-24 | 5 SMP concurrency races fixed (heap, scheduler, per-CPU, timer, page tables). **`current_directory` was NOT in the list of 5.** |
| `f585c6c` | 2026-07-01 | SWAPGS, GDB tracing — mmu.c changes for SWAPGS |

## Key observations

1. `current_directory` was NEVER audited for SMP safety. It was part of the original phase8 code and survived unchanged.
2. The 5-race sweep in `2a5e60d` addressed: heap, scheduler, per-CPU data, timer, and page table allocation — but NOT `current_directory` usage in the page fault handler.
3. The `a02dd47` commit fixed the EXACT same pattern for `task_switch_rsp` and documented it in the ADRs.

## Proof that `current_directory` is the only variable of this class

Other previously-global state that was made per-CPU:
- `_current` (current_task) → per-CPU via GS base (commit `dac4df9`)
- `_task_switch_rsp` → per-CPU via GS base (commit `a02dd47`)
- `current_directory` → **STILL GLOBAL** ← THIS IS THE BUG

## AP behavior analysis

- APs spin in `hlt` loop after init
- AP timer fires → `schedule()` → `task_switch()`
- APs run idle tasks (page_directory = kernel_directory)
- If AP picks a user task (cpu_id == -1), it calls `switch_page_directory(task->pd)` → writes global `current_directory`
- Even if AP stays on idle task and NO context switch happens: `task_switch` returns early at line 1154 WITHOUT calling `switch_page_directory` — safe
- BUT if `next_task != prev_task` (AP picks a new task), it DOES call `switch_page_directory` at line 1193

## Critical race window

1. BSP: `task_switch → switch_page_directory(shell_pd)` sets `current_directory = shell_pd`
2. BSP: Returns to user-mode shell
3. BSP: Shell writes to COW stack → page fault
4. **RACE**: AP timer fires between step 2 and step 3, calls `switch_page_directory(X)`, sets `current_directory = X`
5. BSP: `page_fault → get_page(fault_addr, 0, current_directory)` uses wrong PD (X, not shell_pd)
6. BSP: PML4[255] not in X → get_page returns NULL → COW not resolved → process killed

## Pending verification

- [ ] Reproduce with `-smp 1` to confirm it works on UP → would prove the SMP race is the cause
- [ ] Check if the faulting process's page_directory actually has the correct COW page
