# Architecture: Kernel Core

The Curls Core is the "Sacred Layer" of the system. It is designed to be small, invariant-driven, and rarely changed.

## 1. Core Responsibilities
- CPU bring-up and interrupt management.
- Per-CPU state and stack management.
- Task and process lifecycle (`fork`, `spawn`, `exit`, `reap`).
- Core scheduler interface (mechanism, not policy).
- Virtual memory primitives (Paging, COW).
- Kernel/Module ABI enforcement.

## 2. Boot Sequence
Curls uses a modern 64-bit boot flow via GRUB:
1.  **GRUB Multiboot2**: Loads the ELF64 kernel into 32-bit protected mode.
2.  **32-to-64 Trampoline (`arch/x86_64/boot/`)**: Constructs early 4-level page tables, enables Long Mode, and jumps to 64-bit entry.
3.  **Handoff (`kernel/core/boot_multiboot2_64.c`)**: Parses Multiboot2 tags (memory map, framebuffer) and calls `kernel_main`.
4.  **Kernel Main (`kernel/core/kernel.c`)**: Initializes subsystems in order:
    - **SMP & CPU Local**: Detects cores and sets up `GS_BASE` for per-CPU storage.
    - **GDT/IDT/ISR64**: 64-bit descriptors and interrupt gates.
    - **Paging64 & Heap**: 4-level paging and higher-half heap.
    - **VFS & Multitasking**: SMP-safe tasking and filesystem initialization.

## 3. The Hybrid Stack Model (SMP-Safe)
To ensure stability and isolate core failures:
- **Per-CPU Interrupt Stack**: Each core has a dedicated 8KB interrupt stack defined in the TSS (RSP0).
- **Task Kernel Stack**: Each task has its own 16KB kernel stack for syscall execution.
- **GS_BASE Isolation**: The `GS` segment register points to per-CPU data, allowing fast, lockless access to `current_task` and CPU state.

## 4. Storage and Partitions
The kernel uses a unified block device layer to manage storage:
- **Physical Devices**: Hardware drivers (IDE, USB) register as physical block devices (e.g., `usb0`).
- **Partition Discovery**: Upon registration, the core automatically scans the MBR and registers any detected partitions as virtual block devices (e.g., `usb0p1`).
- **Virtual Offsets**: Partition devices act as transparent proxies, translating relative offsets to absolute LBAs on the parent device.

## 5. SMP Hardening
Curls OS runs on up to 4 cores under both hardware KVM and software TCG emulation (Docker). Key hardening measures:
- **Hardware Memory Barriers**: `mfence` instructions on task switching, signal delivery, and AP synchronization paths ensure cross-core visibility under relaxed emulation ordering.
- **TLB Invalidation on Map**: `mmu_map_page` issues `invlpg` after each mapping to prevent stale MMIO translations (critical for LAPIC init).
- **Null-Resilient Libc**: Global `strlen` and `sys_execve` argument processing guard against null pointers that may arise during SMP race windows.
- **Async Shell Startup**: User-space processes (`INIT.ELF`, `SH64.ELF`) are spawned as background tasks to avoid blocking the K-ABI shell.

## 6. Invariants
- **Fail Fast**: Any kernel-level inconsistency results in an immediate `panic`.
- **Privilege**: Kernel memory is never accessible from user mode (`user=0`).
- **Isolation**: Every process has its own page directory; kernel mappings are shared but protected.
- **Docker Parity**: All core tests must pass identically in both native QEMU/KVM and Docker/TCG environments.
