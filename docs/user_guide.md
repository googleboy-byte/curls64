# Curls OS User Guide

Welcome to the **Curls OS** interactive environment. This guide explains how to navigate the system once you've launched it via Docker or QEMU.

## 🐚 The Dual-Shell Architecture

Curls OS features two distinct shells, each serving a different layer of the system.

### 1. The K-ABI (Sacred) Shell (PID 1)
This is the kernel-space management console. It interacts directly with the **Sacred Core** and is used for low-level validation and system bootstrap.

*   **Prompt**: `K-ABI> `
*   **Purpose**: System stability testing and low-level debugging.
*   **Key Commands**:
    *   `CORE`: Runs the automated 20-phase core verification suite (Paging, SMP, COW, etc.).
    *   `TEST`: Launches the virtual memory and paging unit tests.
    *   `STRESS`: Starts high-concurrency multitasking stress tests.
    *   `PS`: Shows the kernel-level process table.
    *   `MEM`: Displays kernel heap and physical memory statistics.
    *   `USER`: **Handoff to the User Shell**. This launches the ELF64 `/BIN/SH.ELF`.

### 2. The User Shell (sh64) (PID 4)
This is the POSIX-like interactive shell running in **Ring 3**. It is where most "normal" OS interaction happens.

*   **Prompt**: `sh64$ `
*   **Purpose**: Running user applications, scripting, and demonstrating the U-ABI.
*   **Key Features**:
    *   **Standard Utilities**: `ls`, `cat`, `echo`, `mkdir`, `rm`, `cp`, `top`.
    *   **Pipes**: `ls | cat` (Demonstrates kernel IPC via the `pipe()` syscall).
    *   **Scripting**: Run shell scripts with `sh /etc/test.sh`. 
    *   **Ctrl+C**: Gracefully terminates the foreground utility while keeping the shell alive.

---

## 🚀 Recommended Walkthrough

Once you run `make docker64-debug` (or `make run-grub64-verify-debug` locally), follow these steps to explore the system:

1.  **Verify the Kernel**: Type `CORE` in the K-ABI shell. Watch it pass all 20 phases of architectural validation.
2.  **Enter Userland**: Type `USER`. You are now in the 64-bit user shell.
3.  **Explore Files**: Type `ls /etc` and then `cat /etc/test.sh` to see a sample script.
4.  **Run a Script**: Type `sh /etc/test.sh` to see the shell's loop and logic in action.
5.  **Test Stability**: Run `top` and then hit `Ctrl+C`. The shell stays alive, and the utility exits!
6.  **Stress the System**: (Advanced) Exit back to K-ABI and run `STRESS` to see the SMP scheduler handle heavy load.

---

## 🛠️ Debugging Tips

*   **Serial Logs**: When running with `-debug`, check the `logs/` directory for detailed UART output.
*   **Ktrace**: If the system crashes, the `ktrace` module will dump the instruction pointer and stack trace to the serial port.
