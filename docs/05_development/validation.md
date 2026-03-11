# Development: Validation Policy

Curls OS enforces strict input/output contracts at every syscall and K-ABI bridge boundary to ensure system stability.

## 1. Two-Tier Design
- **K-ABI (Kernel-Internal)**: Violations represent a kernel bug and trigger a `panic()`.
- **U-ABI (Syscall Dispatch)**: User-program mistakes trigger a `[VALIDATE FAIL]` diagnostic and return an error code to the caller.

## 2. Mandatory Validation Rules
When adding new K-ABI or U-ABI functions, developers MUST:
1.  **Validate All Pointers**: Use `KABI_VALIDATE_PTR` or `UABI_VALIDATE_PTR`.
2.  **Check Ranges**: Use `KABI_VALIDATE_RANGE` for parameters.
3.  **Verify FDs**: Use `UABI_VALIDATE_FD`.

## 3. Reference Table
| Macro | Layer | Checks | On failure |
|-------|-------|--------|------------|
| `KABI_VALIDATE_PTR` | KABI | `ptr != NULL` | `panic()` |
| `KABI_VALIDATE_RANGE` | KABI | `lo <= val <= hi` | `panic()` |
| `UABI_VALIDATE_PTR` | UABI | `ptr != NULL` | return -1 |
| `UABI_VALIDATE_FD` | UABI | `0 <= fd < MAX_FD` | return -1 |
| `UABI_VALIDATE_OUTPUT` | UABI | `ret >= -5` | warn only |

## 4. Testing Requirements
New validated functions must have corresponding tests in Phase 15 of the core test suite. These must prove:
- ✅ **Positive Case**: Valid inputs are accepted.
- ❌ **Negative Case**: Invalid inputs are correctly rejected with the appropriate error code.
