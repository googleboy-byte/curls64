# Curls OS
*I don't know what I was thinking. I don't know what I am doing.*

A hobbyist **x86_64** operating system written from scratch — **modular kernel**, **stable ABI**, and **expressive shell**.

> **Simplicity First**: Curls is designed for zero-friction boarding. With a single Docker command, you can launch a fully functional 64-bit SMP kernel with an interactive shell and an automated test suite. No complex toolchains, no messy dependencies.

[**📖 Read the User Guide**](./docs/user_guide.md) | [**🏗️ Architecture Docs**](./docs/index.md) | [**🏁 Roadmap**](./docs/01_getting_started/roadmap.md)

---

## 🚀 Key Features

| Area | Status |
|------|--------|
| **Core** | Modular kernel framework with stable K-ABI (x86_64 native) ✅ |
| **SMP** | Foundational Symmetric Multi-Processing (ACPI, LAPIC, IPI, Docker-verified) ✅ |
| **Paging** | 4-level paging, **Copy-on-Write (COW)**, PHYSMAP ✅ |
| **Heap** | Dynamic kernel heap with integrity checks ✅ |
| **Tasks** | Preemptive scheduler, Hybrid stack model, ELF64 support ✅ |
| **Filesystems** | VFS, FAT32, Initrd, Pipes, FDs ✅ |
| **User Mode** | Ring-3 isolation, ELF64 loader, U-ABI v2 ✅ |
| **Shell** | Variables, control flow, pipes, redirections, Ctrl+C handling ✅ |
| **Stability** | 20-phase core test suite, ABI validation, ktrace ✅ |

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
**Dependencies:** `gcc`, `nasm`, `ld`, `qemu-system-x86_64`, `mtools`, `dosfstools`, `python3`

```bash
# Recommended: 64-bit Verification Suite (GRUB + Full Core Tests)
make run-grub64-verify        # Boots into automated 20-phase test suite
make run-grub64-verify-debug  # Above with serial/UART debug logs enabled

# General 64-bit boot
make iso64                    # Build x86_64 GRUB ISO
make run-grub64               # Boot into 64-bit user shell (sh64)

# Legacy 32-bit paths (preserved)
make run                      # Legacy 32-bit BIOS boot
make run-grub                 # 32-bit GRUB boot
```

### Option B: Docker (Recommended)
If you don't want to install dependencies locally, use the provided Docker setup.

```bash
# Build the container
docker compose build

# Quick launch (recommended)
make docker64              # Headless 64-bit verification suite
make docker64-debug        # Above with SMP tracing and UART serial logs

# Or use docker compose directly
docker compose run curls-os-dev make run-grub64-verify

# Fresh build (wipe disk.img)
docker compose run curls-os-dev make clean run-grub64-verify
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