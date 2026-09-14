# Diagnosis: Userland COW Page Fault in sh64

**Date**: 2026-09-14T20:28:28+05:30
**Branch**: `arch/smp` (HEAD at `f585c6c`)
**Stable baseline**: `arch/phase8` (commit `461f9d9`)

---

## 1. Initial Fault

```
Page Fault ( protection-violation write user-mode ) at 0x7fffffffee44
RIP: 0x800216a
PF INSTR at 0x800216a: 0x89 0x85 0x14 0xff    → mov [rbp-0xec], eax
PF REGS: rax=0xd rbx=0x0 rcx=0xa rdx=0x7fffffffeac0
PF REGS: rsi=0x7fffffffeac0 rdi=0x7fffffffecb0 rbp=0x7fffffffef30 rsp=0x7fffffffeaf0
PF REGS: r8 =0x0 r9 =0x0 r10=0x0 r11=0x0
PF REGS: r12=0x0 r13=0x0 r14=0x0 r15=0x0
PF REGS: rip=0x800216a cs =0x1b rfl=0x10202
PF REGS: ursp=0x7fffffffeaf0 ss=0x23
```

**Trigger**: Type `ls bin` (or second/third userland command) in the `sh64` user shell.
**Behavior**: All 21 core test phases pass. Module tests pass. Shell starts. Executing userland commands triggers a page fault after fork/exec, causing the kernel to drop back to `(KABI)>`.

---

## 2. Initial Question

> "All core tests pass, but I can't seem to run any of the userland applications."

The user needs to test userland interactively to verify functionality before proceeding to scheduling IPIs, load balancing, faster syscalls, and modularization.

---

## 3. Verified Root Cause Analysis

### Cause: `current_directory` Global Variable Race in Multi-Core (SMP) Context Switches

1. **Global Variable Definition**:
   In `kernel/cpu/paging.c`, `current_directory` is declared as a single global variable (`page_directory_t *current_directory`).
2. **Single-Core (Phase 8 Baseline)**:
   On UP (uniprocessor), only one CPU runs and updates `current_directory` when switching tasks. The variable always accurately reflects the active CPU's page directory.
3. **SMP Integration (Branch `arch/smp`)**:
   In the SMP branch, Application Processors (APs) run timer interrupts and task context switches concurrently with the Bootstrap Processor (BSP).
4. **The Race Window**:
   - BSP switches to a userland shell/command process and sets `current_directory = shell_pd`.
   - BSP executes userland code. User process triggers a Copy-On-Write (COW) page fault (write to shared user stack or page).
   - **RACE**: Concurrently, an AP timer fires and executes `task_switch()` -> `switch_page_directory(ap_task->page_directory)`.
   - `switch_page_directory` overwrites the global `current_directory` with `ap_task->page_directory` (or `kernel_directory`).
   - BSP's `page_fault` interrupt handler runs. It calls `get_page(faulting_address, 0, current_directory)`.
   - Because `current_directory` was clobbered by the AP, `get_page()` searches the wrong page table hierarchy!
   - `get_page()` returns `NULL`, COW copy/resolution is skipped, and the kernel treats it as an unhandled page fault, terminating/crashing the user process.

---

## 4. Empirical Verification Evidence

1. **Single-CPU (`-smp 1`) Test**:
   Running QEMU with `-smp 1` allows `ls bin`, `echo USERLAND_OK`, and multiple userland commands to execute without any page faults or crashes.
2. **Multi-CPU (`-smp 4`) Test**:
   Running QEMU with `-smp 4` non-deterministically triggers the `current_directory` overwrite race within 1–3 userland command executions.
3. **Code Audit**:
   - `_current` was migrated to per-CPU state (`cpu_local`).
   - `_task_switch_rsp` was migrated to per-CPU state (`cpu_local`).
   - `current_directory` was **left as a global variable**, remaining the last un-migrated task state variable.

---

## 5. Remediation Plan

1. **Migrate `current_directory` to Per-CPU Storage**:
   - Add `current_directory` to `cpu_local_t` struct (or query `get_cpu_local()->current_directory`).
   - Update `switch_page_directory()` to update per-CPU page directory reference.
   - Update `page_fault()` handler in `paging.c` to fetch `get_cpu_local()->current_directory` (or `get_current_task()->page_directory`).
2. **Regression Testing**:
   - Run core unit tests (`make test`).
   - Run multi-command userland stress test under `-smp 4`.
