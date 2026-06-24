# Issues

> Problems that took significant time to debug or resolve.

---

## 2026-06-24 · SMP data races — 5 concurrency bugs across 4 subsystems

**Severity:** HIGH — latent under the current AP scheduling bypass, would cause heap corruption, double-scheduling, and page table races the moment APs begin running real tasks.

**Root cause:**
The kernel was ported from UP to SMP incrementally. Several subsystems still used UP-era `irq_save()`/`irq_restore()` (local-core interrupt mask) or plain reads/writes where SMP requires cross-core synchronisation via spinlocks or atomics.

**Bugs found:**

| # | Subsystem | File(s) | Issue |
|---|-----------|---------|-------|
| 1 | Kernel heap | `libc/kheap.c`, `libc/mem.c` | `alloc()`/`free()` used `irq_save`/`irq_restore` — only masks local core, second core can corrupt heap concurrently |
| 2 | Scheduler | `kernel/core/task.c` | `prev_task->state = TASK_READY` set before `rq_lock`, `next->state = TASK_RUNNING` set after unlock — window where two cores pick the same task |
| 3 | Per-CPU data | `kernel/core/task.c` | `cpu_local[0]` hardcoded in `sys_fork()` and `task_switch()` — APs would read BSP's stack bounds, not their own |
| 4 | Timer / sleep queue | `kernel/cpu/timer.c` | `tick++` non-atomic across cores; sleep queue drain loop walked/unlinked with zero locking |
| 5 | Page tables | `kernel/arch/x86_64/mmu/mmu.c` | `get_or_alloc_table()` writes to `kernel_directory` with no locking — two cores extending the same PML4/PDPT/PD slot race |

**Fix approach:**
Worked in dependency order (1→5) since bugs 2–5 are latent behind bug 1's AP scheduling bypass. Each fix was verified against the full test suite before moving to the next.

**Key design decision:**
For bug 1, `free()` was split into `free_internal()` (lock-free) + public `free()` (acquires `heap_lock`) to avoid deadlock in the `alloc()→expand()→free()` call chain where `alloc()` already holds the lock.

**Time spent:** ~30 minutes (mechanical fixes, identified by prior code review).

**Lesson:** When porting UP→SMP, audit every `irq_save`/`irq_restore` pair — they are not cross-core synchronisation. Every shared mutable structure needs either a spinlock or an atomic operation.
