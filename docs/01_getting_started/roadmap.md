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
- [x] **Per-CPU MMU Isolation**: Per-CPU `current_directory` tracking in `cpu_local_t` preventing TOCTOU races during COW page faults on AP context switches.
- [x] **Driver Spinlock Hardening**: Keyboard ring buffer (`key_buf_lock`) and screen output (`screen_lock`) synchronized for concurrent multi-core access.
- [x] **Yield-Based Zombie Reaping**: Reverted CPU-filtering restriction in `wait_for_children()` and added `hlt` yield in `wait_for_all_children()` loop to eliminate CPU starvation during process cleanup.
- [x] **Hardware Memory Barriers**: `mfence` in task switching, signal delivery, and AP synchronization for TCG/emulator compatibility.
- [x] **Null-Resilient Libc**: `strlen`, `execve` argument processing hardened against null pointer dereference during SMP race windows.
- [x] **Docker/TCG & 4-Core Verified**: Full core test suite (21 phases), module tests, and interactive user shell (`sh64`) verified across 4 cores (`-smp 4`).

## Phase 4: Performance & Optimization (Upcoming)
- [ ] **Fast Syscalls**: Transition from `int 0x80` to `syscall/sysret` for x86_64.
- [ ] **K-ABI Widening**: Finalize high-performance module boundary for 64-bit.
- [ ] **I/O Optimization**: Improve VFS throughput and block device drivers.

## Phase 5: Production SMP & Advanced Concurrency (In Progress 🚧)
- [x] **Multi-Core MMU & Driver Concurrency**: Per-CPU page tables, driver spinlocks, non-blocking zombie wait loops.
- [ ] **TLB Shootdowns**: Explicit memory consistency across cores on page table changes.
- [ ] **Scheduling IPIs**: Cross-core task migration via inter-processor interrupts.
- [ ] **Advanced Scheduling**: Multi-queue scheduler with load balancing.
- [ ] **Cache Hardening**: Explicit management of shared data consistency.

---

## Long-Term Goal
To become a **trusted kernel foundation** that enables rapid OS innovation by making the hard parts (memory, tasks, IRQs) stable and reusable.
