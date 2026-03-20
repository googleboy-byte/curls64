# Curls OS
*I don't know what I was thinking. I don't know what I am doing.*

A hobbyist x86-32 operating system written from scratch: **modular kernel**, **stable ABI**, and **expressive shell**.

---

## 🚀 Key Features

| Area | Status |
|------|--------|
| **Core** | Modular kernel framework with stable K-ABI ✅ |
| **Paging** | 2-level paging, **Copy-on-Write (COW)** ✅ |
| **Heap** | Dynamic kernel heap with integrity checks ✅ |
| **Tasks** | Preemptive scheduler, Hybrid stack model ✅ |
| **Filesystems** | VFS, FAT32, Initrd, Pipes, FDs ✅ |
| **User Mode** | Ring-3 isolation, ELF loader, `INT 0x80` U-ABI ✅ |
| **Shell** | Variables, control flow, pipes, redirections ✅ |
| **Stability** | ABI validation layer, ktrace crash logging ✅ |

---

## 🏗️ Architecture

```
┌────────────────────────────────────────────┐
│               Ring 3 — User Space          │
│  sh.elf  hello.elf  lappy.elf  init.elf   │
│             U-ABI (INT 0x80)               │
├────────────────────────────────────────────┤
│            Syscall Dispatch Layer          │
├──────────────┬─────────────────────────────┤
│   K-ABI v1   │  Stable surface for modules │
├──────────────┴─────────────────────────────┤
│              Kernel Core (Sacred)          │
│  task   │  vfs_core  │  pipe  │  kheap    │
│  paging │  elf_load  │  pmm   │  ktrace   │
└────────────────────────────────────────────┘
```

---

## 🛠️ Quick Start

### Option A: Local Build
**Dependencies:** `gcc`, `nasm`, `ld`, `qemu-system-i386`, `mtools`, `dosfstools`, `python3`

```bash
# Legacy BIOS boot path (direct kernel, old flow)
make clean run        # VGA window
make run-nox          # Headless, legacy boot

# Recommended: GRUB + Multiboot2 boot path (used for USB images)
make iso              # Build GRUB ISO (Multiboot2, framebuffer-aware)
make run-grub         # Boot via GRUB in a VGA window
make run-grub-nox     # Boot via GRUB headless, logs in ./logs
```

### Option B: Docker (Recommended)
If you don't want to install dependencies locally, use the provided Docker setup.

```bash
# Build the container
docker compose build

# Run headless (incremental build)
# This persists your 'disk.img' - changes you make in the shell stay there!
docker compose run curls-os-dev make run-nox

# Fresh build (wipe disk.img)
docker compose run curls-os-dev make clean run-nox
```

---

## 📖 Documentation

Curls documentation is split into logical modules for easier navigation:

- **[Documentation Index](docs/index.md)** - Central entry point.
- **[Getting Started](docs/01_getting_started/introduction.md)** - Vision, goal, and roadmap.
- **[Architecture](docs/02_architecture/kernel_core.md)** - Deep dive into memory, tasks, and core invariants.
- **[Kernel ABI (K-ABI)](docs/03_kabi/contract.md)** - Stable contract for kernel modules.
- **[User ABI (U-ABI)](docs/04_uabi/syscalls.md)** - Syscall reference and shell usage.
- **[Development](docs/05_development/validation.md)** - Validation policies and test suite.

---

## 📜 Project Structure

```bash
curls/
├── boot/      # Bootloader (16 -> 32 bit)
├── kernel/    # Kernel Core + K-ABI Modules
├── include/   # ABI headers (K-ABI & U-ABI)
├── user/      # User-space apps & Shell
├── libc/      # Shared kernel/user helper routines
└── docs/      # Comprehensive documentation
```