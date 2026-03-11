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
Curls uses a custom multi-stage boot process:
1.  **Stage 1 (`boot/bootsect.asm`)**: MBR loader, finds and loads Stage 2.
2.  **Stage 2 (`boot/stage2.asm`)**: Enters Protected Mode, enables A20, loads Kernel.
3.  **Kernel Entry (`kernel/core/kernel_entry.asm`)**: Prepares C environment and calls `kernel_main`.
4.  **Kernel Main (`kernel/core/kernel.c`)**: Initializes subsystems in order:
    - GDT/IDT/ISR
    - Paging & Heap
    - Timer & Keyboard
    - VFS (Initrd/FAT32)
    - Multitasking
    - Launch Init/Shell

## 3. The Hybrid Stack Model
To ensure stability and prevent stack overflows from affecting other tasks:
- **Per-CPU Interrupt Stack**: All interrupts (IRQs/Exceptions) land on a dedicated 8KB stack per CPU.
- **Task Kernel Stack**: Each task has its own 16KB kernel stack.
- **Context Migration**: When switching tasks, the CPU state is saved to the task's private stack, and the TSS is updated to point to the new task's stack for the next interrupt.

## 4. Storage and Partitions
The kernel uses a unified block device layer to manage storage:
- **Physical Devices**: Hardware drivers (IDE, USB) register as physical block devices (e.g., `usb0`).
- **Partition Discovery**: Upon registration, the core automatically scans the MBR and registers any detected partitions as virtual block devices (e.g., `usb0p1`).
- **Virtual Offsets**: Partition devices act as transparent proxies, translating relative offsets to absolute LBAs on the parent device.

## 5. Invariants
- **Fail Fast**: Any kernel-level inconsistency results in an immediate `panic`.
- **Privilege**: Kernel memory is never accessible from user mode (`user=0`).
- **Isolation**: Every process has its own page directory; kernel mappings are shared but protected.
