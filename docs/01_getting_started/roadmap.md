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

## Phase 3: Personality & Ecosystem (Upcoming)
- [ ] **Robotics Personality**: ROS 2 / Micro-ROS substrate support.
- [ ] **Simplified Onboarding**: Documentation and templates for new OS "personalities."
- [ ] **Advanced IPC**: Shared memory and structured message passing.

---

## Long-Term Goal
To become a **trusted kernel foundation** that enables rapid OS innovation by making the hard parts (memory, tasks, IRQs) stable and reusable.
