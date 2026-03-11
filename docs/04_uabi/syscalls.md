# U-ABI: Syscall Reference

User-mode programs (Ring-3) interact with the Curls Kernel via `INT 0x80`. This is the User Application Binary Interface (U-ABI).

## 1. The Syscall Interface
Arguments are passed in registers:
- `EAX`: Syscall Number
- `EBX`, `ECX`, `EDX`: Arguments 1, 2, and 3
- Result is returned in `EAX`.

## 2. Core Syscalls

| Number | Name | Description |
|--------|------|-------------|
| 20 | `OPEN` | Open or create a file. Returns FD. |
| 21 | `READ` | Read bytes from an FD. |
| 22 | `WRITE`| Write bytes to an FD. |
| 23 | `CLOSE`| Close an FD. |
| 24 | `READDIR`| Read directory entries into a buffer. |
| 25 | `GETCWD`| Get current working directory. |
| 26 | `CHDIR`| Change current working directory. |
| 27 | `STAT` | Get file metadata (size, type). |
| 30 | `FORK` | Clone the current process. Returns 0 in child, PID in parent. |
| 31 | `EXEC` | Replace current process image with an ELF binary. |
| 32 | `EXIT` | Terminate the current process. |
| 33 | `WAIT` | Wait for a child process to terminate. |
| 35 | `PIPE` | Create an anonymous pipe. |
| 36 | `DUP2` | Redirect one FD to another. |

## 3. U-ABI Validation
The kernel performs strict validation on all syscall inputs:
- **Pointers**: Must not be NULL and must point to valid user memory.
- **FDs**: Must be within the range `0` to `MAX_FD-1`.
- **Paths**: Must be valid strings within the filesystem.

Violations result in a `[VALIDATE FAIL]` diagnostic in the terminal and a return error (-1 to the user program).
