# Architecture: Processes and Lifecycle

Process management in Curls OS is built around a robust task structure and a hybrid scheduler.

## 1. Process Lifecycle
- **Creation**: Processes are created via `fork()` (cloning an existing task) or `spawn()` (loading an ELF binary into a new task context).
- **Execution**: Tasks transition between `READY`, `RUNNING`, and `WAITING` states.
- **Termination**: A task enters the `ZOMBIE` state upon exit or being killed.
- **Reaping**: Zombie tasks are cleaned up by their parents or by the global orphan reaper (PID 1). This releases memory, closed files, and kernel stacks.

## 2. Scheduling
Curls uses a preemptive multitasking model triggered by the PIT timer (IRQ 0).
- **Core Mechanism**: `task_switch()` saves the current context and picks the next task from the circular `ready_queue`.
- **Pluggable Policy**: The scheduler's logic is defined via the `kabi_scheduler_ops_t` interface. This allows different scheduling algorithms (like Round-Robin) to be registered without modifying the kernel core.
- **Sleep Queue**: Tasks can be placed in a sorted sleep queue to wait for a specific system tick, freeing up CPU time.

## 3. Signals
Signals provide a mechanism for asynchronous communication.
- **Delivery**: `task_deliver_signal()` handles signals like `SIGKILL`, `SIGTERM`, and `SIGINT`.
- **Custom Handlers**: User-mode tasks can register handlers for signals, which the kernel will invoke by injecting a frame into the task's stack.
- **Default Actions**: If no handler is present, signals like `SIGKILL` result in immediate task termination.

## 4. Task Structure (`task_t`)
The `task_t` structure (defined in `kernel/core/task.h`) tracks all process-specific state:
- **PID**: Unique process identifier.
- **Memory**: Private Page Directory.
- **Stacks**: Base and top of the 16KB kernel stack.
- **Files**: Table of open File Descriptors (up to 32 per task).
- **Context**: Saved `ESP` and `EIP`.
- **Hierarchy**: Pointers to parent and siblings in the task list.
