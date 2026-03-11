# System Monitor (sysmon) - K-ABI Module

A Linux `top`-like system monitoring module built entirely on the Curls Kernel K-ABI v1.

## Overview

This module provides real-time system monitoring capabilities similar to Linux's `top` command. It displays:
- Process information (PID, state, stack pointers, capabilities)
- Memory statistics (heap and physical memory usage)
- System uptime

## Design Philosophy

The module adheres to K-ABI principles:
1. **Uses only K-ABI functions** - No direct access to kernel internals
2. **Opaque Iteration** - Uses `kabi_task_iter_t` to traverse process list safely
3. **Semantic operations** - Queries system state through stable ABI calls
4. **Modular initialization** - Clean init/API separation

## Files

- `sysmon_top.h` - Public API header
- `sysmon_top.c` - Implementation using only safe K-ABI iterators

## API Functions

### Initialization
```c
void sysmon_init(void);
```
Initialize the system monitor module. Call this during kernel boot.

### System Overview
```c
void sysmon_top(void);
```
Displays a system snapshot. Uses `kabi_task_next()` to iterate tasks.

### Detailed Process List
```c
void sysmon_ps_verbose(void);
```
Show detailed information for each process.

### Memory Statistics
```c
void sysmon_mem_stats(void);
```
Display detailed memory statistics.

## Integration

See `INTEGRATION.md` for build instructions.

## K-ABI Functions Used

### Task Iteration (New!)
- `kabi_task_iter_begin()` - Initialize iterator
- `kabi_task_next()` - Get next task snapshot

### Memory
- `kabi_get_heap_stats()`
- `kabi_get_pmm_stats()`

### Utils
- `kabi_get_ticks()`
- `kprint()`, `kabi_int_to_ascii()`, `kabi_hex_to_ascii()`

## Safe Iteration Pattern

The module avoids accessing internal kernel lists like `ready_queue` directly. Instead:

```c
kabi_task_iter_t it;
kabi_task_info_t info;

if (kabi_task_iter_begin(&it) == KABI_SUCCESS) {
    while (kabi_task_next(&it, &info)) {
        // use info...
    }
}
```

This ensures the kernel can change its internal task storage without breaking the module.

## License

Part of the Curls Kernel project.
