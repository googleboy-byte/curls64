# Introduction and Vision

## 1. Executive Summary

Curls is a **modular kernel framework** designed to be the "Python of Operating Systems." It provides a low-ceremony, high-leverage foundation for beginners, researchers, and developers to prototype complete OS environments quickly and safely.

Curls prioritizes **correctness, explicit invariants, and composability** over maximal features or strict POSIX compatibility.

---

## 2. Core Vision

### 2.1 What Curls Is
* A **kernel construction kit**, not a monolithic OS.
* A stable, invariant-driven core with pluggable subsystems.
* A platform for experimentation in robotics, IoT, unikernels, and education.

### 2.2 What Curls Is Not
* Not a Linux replacement or a full POSIX kernel.
* Not a general-purpose desktop OS.
* Not a codebase where the core is modified for every minor feature.

---

## 3. Design Philosophy

### 3.1 Progressive Disclosure of Complexity
Beginners get sane defaults; experts get full control. Complexity is layered, never hidden.

Broken invariants cause immediate `panic()`. Undefined behavior is surfaced early. No "mostly works" paths.

### 3.3 Prerequisites
- **Dependencies**: `x86_64-elf-gcc`, `nasm`, `ld`, `qemu-system-x86_64`, `mtools`, `dosfstools`, `python3`.

### 3.4 Policy at the Edges, Mechanism at the Core
The core provides the *how* (mechanism); modules provide the *what* (policy).

---

## 4. Current State (v0.6+)

### 4.1 Architectural Invariants
* **Symmetric Multi-Processing (SMP)**: Foundational support for multiple cores (ACPI/LAPIC).
* **Hybrid Stack Model**: Dedicated task stacks + per-CPU interrupt stacks.
* **4-Level Paging**: Full x86_64 long-mode address space.
* **Preemptive Multitasking** with deterministic scheduling and spinlock-safety.

### 4.2 Process & Lifecycle
* `fork()` and `spawn()` support (ELF64).
* Automatic **Zombie Reaping** via PID 1 (global orphan reaper).
* Persistent kernel shell (PID 1) and 64-bit user shell (PID 4).

### 4.3 Memory System
* **Dynamic Kernel Heap**: Expands as needed in the higher-half.
* **Copy-on-Write (COW)**: Fully implemented for efficient forking.
* **Linear PHYSMAP**: Direct access to physical memory from the kernel.

---

## 5. Non-Goals
* Full POSIX compliance.
* Supporting all possible hardware (Primary target: x86_64/QEMU/UEFI).
* Feature parity with mature monolithic kernels.
