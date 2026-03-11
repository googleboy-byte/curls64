# SYSMON Integration Guide

This document describes the integration of the `sysmon` (System Monitor) module into the Curls kernel.

## What Was Done

### 1. Module Files Created

Created three files in `kernel/modules/sysmon/`:

- **sysmon_top.h** - Header file with public API declarations
- **sysmon_top.c** - Implementation using only K-ABI functions
- **README.md** - Comprehensive documentation for the module

### 2. Kernel Integration

Modified `kernel/core/kernel.c`:
- Added external declaration: `extern void sysmon_init();`
- Added module initialization call: `sysmon_init();` (after `shell_init()`)

### 3. Shell Commands

Modified `kernel/modules/shell/kernel_shell.c`:
- Added external declarations for sysmon functions
- Added three new commands:
  - `TOP` - Display system overview (like Linux top)
  - `MEMSTAT` - Show detailed memory statistics
  - `PSV` - Show verbose process information
- Updated HELP text to include new commands

### 4. Build System

Modified `Makefile`:
- Added `kernel/modules/sysmon/*.c` to `C_SOURCES`
- Added `kernel/modules/sysmon/*.h` to `HEADERS`

## Module Features

The sysmon module provides:

1. **System Overview (TOP command)**
   - System uptime in ticks
   - Heap memory usage (total/used/free in KB)
   - Physical memory usage (total/used/free in KB)
   - Process table with PID, state, ESP, EIP, capabilities, parent PID
   - Total process count

2. **Verbose Process List (PSV command)**
   - Detailed per-process information
   - User and kernel stack addresses
   - Full capability flags
   - Parent process relationships

3. **Memory Statistics (MEMSTAT command)**
   - Detailed heap statistics (bytes and KB)
   - Physical memory frame statistics
   - Memory usage percentage
   - Maximum heap address

## K-ABI Compliance

The module uses **only** K-ABI functions:

### Memory Management
- `kabi_get_heap_stats()` - Retrieve kernel heap statistics
- `kabi_get_pmm_stats()` - Retrieve physical memory statistics

### Time
- `kabi_get_ticks()` - Get system uptime

### Console I/O
- `kprint()` - Print strings to console
- `kabi_int_to_ascii()` - Convert integers to strings
- `kabi_hex_to_ascii()` - Convert hex values to strings

## Building and Testing

1. **Clean build:**
   ```bash
   make clean
   make
   ```

2. **Run the kernel:**
   ```bash
   make run
   ```

3. **Test commands in the shell:**
   ```
   (KABI)> HELP
   (KABI)> TOP
   (KABI)> MEMSTAT
   (KABI)> PSV
   ```

## Architecture Notes

### Task Structure Mirror

The module maintains a local mirror of the task structure to iterate through the process list. This is a bridge pattern until K-ABI provides a dedicated task iteration API.

The mirror is defined as:
```c
typedef struct {
    uint32_t magic;
    uint32_t id;
    uint32_t user_esp;
    uint32_t user_eip;
    uint32_t kernel_stack;
    uint32_t kernel_stack_base;
    void *page_directory;
    void *parent;
    volatile uint8_t state;
    uint32_t capabilities;
    void *fd_table[32];
    void *next;
} task_mirror_t;
```

This matches the internal `task_struct` but is treated as opaque by the K-ABI.

### External Symbols

The module accesses two external kernel symbols:
- `ready_queue` - Points to the circular task list
- `current_task` - Points to the currently running task

These are temporary until K-ABI provides task enumeration functions.

## Design Principles Demonstrated

1. **Separation of Concerns**: Module is completely separate from kernel core
2. **Stable ABI**: Uses only documented K-ABI functions
3. **Linkable**: Can be easily added/removed from build
4. **Modular Init**: Clean `init()` function for registration
5. **No Internal Dependencies**: Doesn't access kernel internals directly

## Future Enhancements

Possible improvements:
- Add K-ABI task iteration functions to eliminate task_mirror
- Track CPU usage per process
- Add auto-refresh capability
- Implement process filtering and sorting
- Add thread information display
- Track I/O statistics
