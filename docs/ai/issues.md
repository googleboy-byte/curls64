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

---

## 2026-09-30 · SMP 4-Core Userland Stabilization — 5 concurrency & task handling bugs

**Severity:** CRITICAL — Caused userland applications (`sh64`) to crash with Copy-On-Write (COW) page faults or hang in process cleanup loops under 4-core multi-processor execution (`-smp 4`).

**Root Cause & Bugs Found:**

| # | Subsystem | File(s) | Issue & Fix |
|---|-----------|---------|-------------|
| 1 | Paging / MMU | `kernel/cpu/paging.c`, `kernel/arch/x86_64/mmu/mmu.c` | Global `current_directory` variable overwritten when AP context switches, causing BSP COW page faults to read wrong PD and fail. **Fix**: Moved `current_directory` into `cpu_local_t` (per-CPU). |
| 2 | Keyboard Driver | `kernel/modules/drivers/keyboard.c` | Unsynchronized key ring buffer access between IRQ handler and userland polling tasks. **Fix**: Added `key_buf_lock` spinlock. |
| 3 | Screen / UART Driver | `kernel/modules/drivers/screen.c` | Concurrent writes to UART serial port and screen buffer garbled output. **Fix**: Added `screen_lock` spinlock. |
| 4 | Task Reaping | `kernel/core/task.c` | `wait_for_children()` restricted zombie matching by CPU ID, locking up parent tasks waiting for children that executed on APs. **Fix**: Reverted to standard `TASK_ZOMBIE` check. |
| 5 | Scheduler Wait Loop | `kernel/core/task.c`, `user/sh/sh.c` | `wait_for_all_children()` executed a tight CPU-starving busy loop. **Fix**: Added `hlt` yield between iterations. |

**Verification:**
Executed 21 core kernel test phases and interactive userland tests (`ls`, `echo`, `cat`, `sysinfo`, `pwd`) across two consecutive full boot-to-shutdown sessions under `-smp 4`.

**Lesson:**
Global pointer variables (like page directory references) inherited from UP design are silent SMP hazards. Every task state reference during exception/interrupt handling must be fetched from per-CPU context (`cpu_local_t`).


---

## 2026-10-04 · SMP AP Stall — Kernel-mode fork inherits IF=0

**Severity:** CRITICAL — Caused one AP to permanently stall per boot under `-smp 4`. Non-deterministic victim CPU; always PID 4 (first signal-test child).

**Root cause:**
`sys_fork` copied the parent's interrupt frame to the child's kernel stack without sanitizing RFLAGS. The `int $0x80` interrupt-gate entry clears IF in saved RFLAGS. A kernel-mode child with IF=0 in its frame that executed `for(;;) { hlt; }` permanently halted the CPU — timer interrupts never fired to wake it.

Confirmed by `[FORK] WARN: child PID N inherited IF=0` appearing for every kernel-mode fork (PIDs 4, 5, 6, 7, 8, 10) with RFLAGS=0x6.

**Fix:**
Sanitize child RFLAGS in `sys_fork` Phase 4.2 (`task.c`):
```c
child_regs->rflags = (child_regs->rflags | 0x202) & ~(0x100 | 0x4000 | 0x3000);
```
Forces IF + reserved bit; clears TF, NT, IOPL.

**Additional changes:**
- `_stall_code` diagnostic field removed from `cpu_local_t`; replaced by `_previous_task` for deferred cpu_id clear (H2 race defence).
- `ASSERT_IF(tag)` macro added to `cpu_local.h`; placed between every signal test to bisect which test leaves IF=0.
- Core test phases expanded from 21 to 23: Phase 22 (fork RFLAGS regression gate), Phase 23 (AP liveness gate — all CPUs must tick over 500ms window).

**Verification:**
All 4 CPUs showed delta >= 24 ticks over 500ms probe. DIAG: CPU 1/2/3 all `ok`, no ZOMBIE-TASK, no STUCK-IN-IRQ.

**Open:** IF=0 root cause (which caller path runs with interrupts off) not yet identified. ASSERT_IF probes are in place for next boot.

**Lesson:** An interrupt-gate `int` instruction clears IF in saved RFLAGS. Any kernel-mode fork must sanitize the child frame — the parent's RFLAGS at `int $0x80` time is unpredictable. `hlt` with IF=0 is a permanent CPU halt; no trap or panic results.
