# Development: Testing Suite

Curls OS includes a comprehensive diagnostic and stress testing suite to verify kernel stability.

## 1. Kernel Diagnostic Tests (`TEST`)
Run from the K-ABI shell. These verify core subsystems and memory protection.
- **`TEST PAGING`**: Verifies NULL pointer dereference, OOB access, and kernel-space protection.
- **`TEST HEAP`**: Runs allocation/free cycles to verify block integrity.
- **`TEST FD`**: Validates file descriptor duplication and redirection.
- **`TEST PIPE`**: Tests synchronous and asynchronous communication.

## 2. Multitasking Stress Tests (`STRESS`)
Designed to push the scheduler and resource management to their limits.
- **`STRESS FORK`**: Spawns tasks until `MAX_TASKS` is reached. Verifies PMM and Heap expansion.
- **`STRESS DEEP`**: Recursive syscall nesting to push the kernel stack limits.
- **`STRESS RACE`**: Fast cyclical `fork()` -> `exit()` -> `reap()` to test queue integrity.

## 3. Running All Tests
To run the automated validation phase of the core test suite:
```bash
make run
# At K-ABI prompt
ktest core
```
Phase 15 specifically exercises the ABI validation macros.

## 4. Test Locations
- **Core Tests**: `kernel/core/tests/`
- **Module Tests**: `kernel/modules/tests/`
- **Shell Tests**: `user/sh/tests/`
