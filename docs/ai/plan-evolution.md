# Plan Evolution

> Tracks how the system design changed over time — pivots, not a diary.

---

## 2026-09-30 · Pivot: Per-CPU MMU Tracking and Driver Spinlocks for SMP Parity

- **Original Plan**: Assume basic AP boot and interrupt spinlocks were sufficient for SMP userland execution.
- **Pivot**: Discovered that UP legacy code contained hidden global state (specifically `current_directory` page table pointer) and driver ring buffers without spinlocks.
- **Action**: Refactored `current_directory` to per-CPU `cpu_local_t` storage, introduced driver spinlocks (`key_buf_lock`, `screen_lock`), and added `hlt` yield to child zombie wait loops.
- **Outcome**: Reached 100% functional parity between single-core (`-smp 1`) and 4-core multi-core (`-smp 4`) execution across all 21 core kernel test phases and interactive userland (`sh64`).

