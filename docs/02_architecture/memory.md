# Architecture: Memory Management

Curls OS employs a layered memory management system designed for safety and efficiency.

## 1. Physical Memory Manager (PMM)
The PMM uses a **Bitmap** to track free and used 4KB frames.
- **Location**: `kernel/cpu/paging.c`
- **Capabilities**: Frame allocation, freeing, and reference counting.
- **Early Reservation**: Protects kernel code and boot-time data structures.

## 2. Paging & Virtual Memory
Curls implements a 2-level paging scheme (Page Directory and Page Tables).
- **Identity Mapping**: The first 128MB (or total RAM) is identity mapped for kernel convenience.
- **PHYSMAP**: A dedicated linear mapping at `0xE0000000` allows the kernel to access any physical frame by address.
- **Write Protection (WP)**: Enabled in `CR0` to enforce read-only segments and support COW.

## 3. Copy-on-Write (COW)
COW allows `fork()` to be near-instant by sharing memory pages between parent and child.
- **Mechanism**: Pages are marked as read-only and `cow=1`.
- **Fault Handling**: When a write occurs, a Page Fault (IRQ 14) is triggered. The kernel allocates a new frame, copies the data via PHYSMAP, and updates the page table to be writable.
- **Reference Counting**: Frames are only freed when their reference count (tracked in PMM) reaches zero.

## 4. Kernel Heap
The kernel heap provides dynamic memory allocation (`kmalloc`/`kfree`).
- **Expansion**: If the heap runs out of space, it automatically requests more pages from the paging system.
- **Safety**: Includes guard bits and integrity checks to detect heap corruption.
- **Stats**: Use the `MEM` command in the K-ABI shell to view live heap usage.

## 5. Memory Map
| Virtual Address | Purpose |
|-----------------|---------|
| `0x00000000` | Real Mode IVT & Kernel Code |
| `0x00090000` | CPU Interrupt Stacks |
| `0x00100000` | Ktrace Buffer (Crash Logs) |
| `0xC0000000` | Kernel Heap Start |
| `0xE0000000` | Linear PHYSMAP |
