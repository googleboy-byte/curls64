# Project Roadmap

Curls OS is developed in phases, focusing on building a rock-solid core before expanding into pluggable modules.

## Phase 1: The Sacred Core (Completed ✅)
- [x] **Formalize Core Invariants**: Per-CPU stacks, hybrid scheduling.
- [x] **Copy-on-Write (COW)**: Efficient process forking.
- [x] **Stable U-ABI**: Documented syscall interface.
- [x] **Kernel Heap**: Dynamic expansion and integrity checks.

## Phase 2: Modularization (In Progress 🚧)
- [ ] **Scheduler Plugin Framework**: Move scheduling policy out of the core into K-ABI modules.
- [ ] **Driver Module Interface**: Standardize how hardware drivers (VGA, Keyboard, Disk) interact with the core.
- [ ] **Basic Module Loader**: Support for loading `.mod` files at runtime.
- [x] **K-ABI v1**: Stable surface for early module development.

- [ ] **Advanced IPC**: Shared memory and structured message passing.
- [/] **Robotics Personality**: ROS 2 / Micro-ROS substrate support.

## Phase 3: SMP Stabilization (Completed ✅)
- [x] **ACPI/LAPIC/IOAPIC**: BSP and AP initialization with MADT parsing.
- [x] **Per-CPU State**: GS_BASE-based `cpu_local` isolation, per-CPU TSS and kernel stacks.
- [x] **Hardware Memory Barriers**: `mfence` in task switching, signal delivery, and AP synchronization for TCG/emulator compatibility.
- [x] **Null-Resilient Libc**: `strlen`, `execve` argument processing hardened against null pointer dereference during SMP race windows.
- [x] **Docker/TCG Verified**: Full core test suite (20 phases), module tests, and user-space utility script pass in headless Docker.

## Phase 4: Performance & Optimization (Upcoming)
- [ ] **Fast Syscalls**: Transition from `int 0x80` to `syscall/sysret` for x86_64.
- [ ] **K-ABI Widening**: Finalize high-performance module boundary for 64-bit.
- [ ] **I/O Optimization**: Improve VFS throughput and block device drivers.

## Phase 5: Production SMP (Upcoming)
- [ ] **TLB Shootdowns**: Ensure memory consistency across cores on page table changes.
- [ ] **Scheduling IPIs**: Cross-core task migration via inter-processor interrupts.
- [ ] **Advanced Scheduling**: Multi-queue scheduler with load balancing.
- [ ] **Cache Hardening**: Explicit management of shared data consistency.

---

## Long-Term Goal
To become a **trusted kernel foundation** that enables rapid OS innovation by making the hard parts (memory, tasks, IRQs) stable and reusable.
