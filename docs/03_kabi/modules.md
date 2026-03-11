# K-ABI: Writing Modules

Curls OS modules are separate units that provide specific policies or hardware support without modifying the core kernel code.

## 1. Module Basics
All kernel modules should include `include/kabi/kabi_v1.h`. They interact with the system only through the defined K-ABI routines.

## 2. Writing a Scheduler Module
A scheduler module must implement the `kabi_scheduler_ops_t` structure:
```c
kabi_scheduler_ops_t my_sched = {
    .name = "MyCustomScheduler",
    .pick_next = my_pick_next_func,
    .on_task_added = my_task_added_hook,
    .on_task_removed = my_task_removed_hook
};

// In module initialization
kabi_scheduler_register(&my_sched);
```

## 3. Writing a Driver Module
Drivers register themselves with the VFS or the IRQ system.
- **Device Registration**: Create an `fs_node_t` and register it with `kabi_vfs_register`.
- **Interrupt Handling**: Use `kabi_irq_register` to catch hardware IRQs.

## 4. Best Practices
- **No Direct Access**: Never access `ready_queue` or `current_task` directly. Use `kabi_get_current_task_id()`.
- **Validation**: Ensure your module handles error returns from K-ABI calls gracefully.
- **Memory**: Always `kfree` any memory allocated with `kmalloc` when no longer needed.

## 5. Module Layout
Modules are typically located in `kernel/modules/`.
- `drivers/`: Keyboard, VGA, IDE, etc.
- `sched_rr/`: The default Round-Robin scheduler.
- `fs_initrd/`: The Initial RAM Disk driver.
