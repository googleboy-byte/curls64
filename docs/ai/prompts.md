# AI Usage — Prompts Log

> Every non-trivial AI interaction is logged here with context, prompt, response summary, and what was used or discarded.

---

## 2026-06-24 · SMP concurrency audit — race conditions in AP startup and TLB shootdown

**Prompt:**
Reviewed a set of 4 potential issues + 3 minor suggestions flagged against the latest SMP commits (ap_ready_flags atomicity, LAPIC_EOI magic constant, dirty pending_mask on shootdown timeout, lock-free handler reads, MAX_CPUS drift, test coverage gap, busy-delay calibration). Asked AI to cross-check each against the actual codebase, discard hallucinated issues, and produce an implementation plan for the real ones.

**Response summary:**
AI verified all 4 main issues against the source. Confirmed 3 as real bugs (non-atomic `ap_ready_flags |=`, dirty `pending_mask` on timeout, magic `0x0B0` constant) and 1 as a valid-but-safe concern needing documentation (lock-free handler read). Confirmed the `MAX_CPUS` drift hazard as real. Correctly discarded the test coverage and TSC delay items as not actionable. Produced a targeted implementation plan with file-level diffs.

**What we used / didn't use:**
Used all 5 proposed fixes directly — they were mechanical and clearly correct after code verification. The protocol invariant comment for issue #4 was adopted as-is since the ordering analysis was accurate. Discarded the test coverage and TSC delay suggestions per the plan (acknowledged, not worth the complexity right now).

---

## 2026-06-24 · SMP concurrency hardening — fix 5 confirmed data races in kernel subsystems

**Prompt:**
Given 5 confirmed concurrency bugs (with exact line numbers) identified by a prior code review, plus a deferred 6th item (enable AP scheduling). Instructed to fix in dependency order 1→5, verifying the test suite after each fix, and to not attempt item 6 until 1–5 were confirmed clean.

**Response summary:**
AI worked through all 5 bugs incrementally with build verification after each:
1. **Heap allocator** (`kheap.c`, `mem.c`): Replaced `irq_save`/`irq_restore` with `spin_lock_irqsave`/`spin_unlock_irqrestore` on a new `heap_lock`. Split `free()` into a lock-free `free_internal()` + public wrapper to avoid deadlock in `expand()→free()` path. Same treatment for `kmalloc_int()` with its own `kmalloc_int_lock`.
2. **Scheduler race** (`task.c`): Moved `prev_task->state = TASK_READY` inside the `rq_lock` critical section, and moved `current_task->state = TASK_RUNNING` before `spin_unlock(&rq_lock)`, so no other core can pick a task that's mid-context-save.
3. **Hardcoded `cpu_local[0]`** (`task.c`): Replaced 5 instances in `sys_fork()` and `task_switch()` with `get_cpu_local()` pointer, so the code reads the correct per-CPU struct on any core.
4. **Timer tick + sleep queue** (`timer.c`): Changed `tick++` to `__sync_add_and_fetch(&tick, 1)` for atomicity. Wrapped the sleep queue drain loop in `spin_lock(&sq_lock)`/`spin_unlock(&sq_lock)`, sharing the lock already used by `sleepq_insert()`/`sleepq_remove()` in task.c (required making `sq_lock` non-static).
5. **Page table allocation** (`mmu.c`): Added `pgtable_lock` spinlock, acquired only when operating on `kernel_directory` in `mmu_map_page()` and `get_page()`. Per-process directories are not shared cross-core and don't need locking.

**What we used / didn't use:**
Used all 5 fixes as proposed. Each was mechanically verified via `make run-grub64-verify` (all 21 test phases + 18 UABI validation tests pass). Item 6 (enable AP scheduling) deferred per plan — requires separate confirmation before attempting.
