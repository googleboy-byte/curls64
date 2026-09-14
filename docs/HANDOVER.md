# Curls64 OS Development Handover Summary

**Date**: September 14, 2026
**Branch**: `arch/smp` (HEAD: `f585c6c`)
**Baseline Stable**: `arch/phase8` (`461f9d9`)

---

## 1. Executive Summary

We investigated an issue where **all 21 core kernel tests pass**, but userland applications crash with a **Page Fault** after executing 1–3 commands in `sh64` under QEMU multi-core mode (`-smp 4`).

Through git diff analysis, CPU execution tracing, and single vs. multi-core verification, we definitively isolated the root cause: a **TOCTOU race on the global `current_directory` page directory pointer during SMP task switching**.

---

## 2. Diagnosis & Root Cause Analysis

### The Bug Mechanism

- **Global Variable**: In `kernel/cpu/paging.c`, page directory tracking uses a single global variable:
  ```c
  page_directory_t *current_directory;
  ```
- **Single-Core (Phase 8)**: Worked fine on single-CPU systems because only one CPU updated or read `current_directory`.
- **Multi-Core (SMP)**:
  1. **BSP (CPU 0)** executes `sh64` / user process. It sets `current_directory = shell_pd` and enters user mode.
  2. The process performs a write to a Copy-On-Write (COW) stack/heap page, triggering a Page Fault (`0x0e`).
  3. **Concurrent Race**: An Application Processor (AP, CPU 1-3) receives a timer interrupt and calls `task_switch()` -> `switch_page_directory(ap_task->page_directory)`.
  4. `switch_page_directory()` overwrites the global `current_directory` with `ap_task->page_directory` or `kernel_directory`.
  5. BSP's `page_fault()` handler runs and executes:
     ```c
     page_t *page = get_page(faulting_address, 0, current_directory);
     ```
  6. Because `current_directory` was clobbered by the AP, `get_page()` searches the wrong page table structure. It returns `NULL`, causing COW resolution to fail and the kernel to terminate the user process.

### Empirical Verification Results

| Configuration | Test Execution | Result |
| :--- | :--- | :--- |
| **`-smp 1` (Single CPU)** | `ls bin`, `echo USERLAND_OK`, shell commands | **PASSED** (0 Page Faults, 100% reliable) |
| **`-smp 4` (Multi CPU)** | `ls bin`, userland execution | **FAILED** (Page fault due to `current_directory` race) |

---

## 3. Immediate Task: Fix `current_directory` for SMP

To fix the crash:
1. Move `current_directory` into per-CPU storage (e.g. `get_cpu_local()->current_directory` or struct `cpu_local_t`).
2. Update `switch_page_directory()` in `kernel/arch/x86_64/mmu/mmu.c` to update the active CPU's per-CPU page directory.
3. Update `page_fault()` in `kernel/cpu/paging.c` to read the per-CPU `current_directory` (or fetch `get_current_task()->page_directory`).
4. Validate both `-smp 1` and `-smp 4` executions.

---

## 4. Overall Roadmap & Next Steps

Once the `current_directory` SMP race fix is applied and verified:

1. **Userland Validation**: Verify `sh64`, `ls64`, `cat64`, `sysinfo64`, and other binary executables under multi-core QEMU.
2. **Scheduling IPIs & Load Balancing**: Implement Inter-Processor Interrupts (IPIs) for rescheduling and work-stealing/load-balancing across AP cores.
3. **Faster Syscalls**: Implement x86_64 `SYSCALL`/`SYSRET` fast system call handlers (MSR `IA32_LSTAR`, `IA32_STAR`, `IA32_FMASK`).
4. **Modularization**: Refactor kernel sub-systems into clean dynamic/loadable modules.

---

## 5. Handover Script for Next Session

Copy and paste the prompt below into the next AI chat session to seamlessly resume work:

```markdown
We are resuming work on the Curls64 OS kernel (`arch/smp` branch).

### Status Summary:
- Core kernel tests (21 phases) and module tests pass cleanly.
- We investigated a userland crash (`sh64` page fault at stack/heap write during `ls bin` or subsequent commands).
- **Verified Root Cause**: `current_directory` in `kernel/cpu/paging.c` is a global variable. On SMP (`-smp 4`), AP timer task-switches overwrite `current_directory`, causing BSP page-fault handlers (`page_fault()`) to inspect the wrong page table and fail COW resolution.
- **Empirical Verification**: `-smp 1` runs userland perfectly without faults. `-smp 4` reproduces the global variable race.
- Documentation & diagnosis notes are in `docs/diagnosis/` and `docs/HANDOVER.md`.

### Your Task for This Session:
1. Refactor `current_directory` to be per-CPU (in `cpu_local_t` / `get_cpu_local()`).
2. Update `switch_page_directory()` and `page_fault()` to read/write the per-CPU page directory.
3. Build and test using `make` / QEMU to verify userland stability under `-smp 4`.
4. Proceed to the next roadmap milestone: Scheduling IPIs and Load Balancing.
```
