# K-ABI: The Core Contract

The Kernel ABI (K-ABI) is the stable interface between the Curls Core and pluggable modules. It defines the "Sacred Layer" and ensures that extensions cannot break core invariants.

## 1. The Sacred Layer
The Curls Core is an invariant-driven frame. Its primary job is to provide safe mechanisms for modules to use.
- **Stability**: The core changes rarely. Any change requires design rationale and invariant analysis.
- **Isolation**: Modules must never touch kernel internal structures directly. They must use the functional interface provided by `kabi_v1.h`.

## 2. Module Registration
Modules extend the kernel by registering their own logic via stable pointers.
- **Schedulers**: `kabi_scheduler_register(ops)`
- **VFS Drivers**: `kabi_vfs_register(node)`
- **IRQ Handlers**: `kabi_irq_register(int_no, handler)`

## 3. ABI Enforcement
Curls uses a validation layer to enforce these contracts at runtime.
- **K-ABI Bridge**: Every call from a module into the core passes through the K-ABI bridge, which validates pointers, ranges, and alignment.
- **Fail Loudly**: If a module violates the contract (e.g., passing a NULL pointer to `kmalloc`), the kernel will `panic()` to prevent silent data corruption.

## 4. Key Interfaces (`include/kabi/kabi_v1.h`)
- **Memory**: `kmalloc`, `kfree`, `kabi_get_heap_stats`.
- **Tasks**: `kabi_fork`, `kabi_task_signal`, `kabi_yield`.
- **VFS**: `kabi_vfs_read`, `kabi_vfs_write`, `kabi_vfs_resolve`.
- **Hardware**: `kabi_irq_register`, `kabi_outb`, `kabi_inb`.
