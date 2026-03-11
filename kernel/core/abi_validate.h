#ifndef ABI_VALIDATE_H
#define ABI_VALIDATE_H

/**
 * ╔═══════════════════════════════════════════════════════════════════╗
 * ║          ABI Validation Layer — Loud Failure Framework           ║
 * ╚═══════════════════════════════════════════════════════════════════╝
 *
 * PURPOSE:
 *   Catch invalid inputs at ABI boundaries BEFORE they corrupt state.
 *   Every violation produces a [VALIDATE FAIL] tagged diagnostic line.
 *
 * TWO TIERS:
 *   KABI_VALIDATE_*  → panic() on failure (kernel-internal callers)
 *                      Violations here mean KERNEL BUG — crash immediately.
 *                      Used in kabi_bridge.c for all K-ABI functions.
 *
 *   UABI_VALIDATE_*  → kprint diagnostic + return error to user
 *                      Violations here mean USER MISTAKE — never crash.
 *                      Used in syscall_dispatch.c for all U-ABI syscalls.
 *                      These macros require `regs` in scope and a
 *                      `syscall_done:` label at the end of the handler.
 *
 * ════════════════════════════════════════════════════════════════════
 *  MANDATORY POLICY — READ BEFORE ADDING NEW ABI FUNCTIONS
 * ════════════════════════════════════════════════════════════════════
 *
 *  1. Every new KABI function in kabi_bridge.c MUST validate ALL
 *     pointer parameters with KABI_VALIDATE_PTR, all numeric ranges
 *     with KABI_VALIDATE_RANGE, and all non-zero requirements with
 *     KABI_VALIDATE_NONZERO.
 *
 *  2. Every new UABI syscall case in syscall_dispatch.c MUST validate
 *     pointer args with UABI_VALIDATE_PTR, fd args with
 *     UABI_VALIDATE_FD, positive counts with UABI_VALIDATE_POSITIVE,
 *     and bounded values with UABI_VALIDATE_RANGE.
 *
 *  3. Output validation: use UABI_VALIDATE_OUTPUT on return values
 *     from kernel functions to catch unexpected out-of-range returns.
 *
 *  4. Every new validation MUST be accompanied by a corresponding
 *     test in Phase 15 of core_test_v1.c that proves:
 *       (a) Valid inputs are accepted (positive test)
 *       (b) Invalid inputs are rejected with the correct error code
 *           (negative test)
 *
 *  5. The [VALIDATE FAIL] tag is machine-parseable. All diagnostics
 *     MUST use this exact prefix for automated log scanning.
 *
 * ════════════════════════════════════════════════════════════════════
 *  MACRO REFERENCE
 * ════════════════════════════════════════════════════════════════════
 *
 *  KABI macros (kernel — panic on failure):
 *    KABI_VALIDATE_PTR(ptr, "func")         — ptr != NULL
 *    KABI_VALIDATE_RANGE(val, lo, hi, "func") — lo <= val <= hi
 *    KABI_VALIDATE_NONZERO(val, "func")     — val != 0
 *    KABI_VALIDATE_ALIGNED(val, N, "func")  — val % N == 0
 *    KABI_VALIDATE_OUTPUT(ret, expected, "func") — ret == expected
 *
 *  UABI macros (user — return error, never panic):
 *    UABI_VALIDATE_PTR(ptr, syscall_num)           — ptr != NULL
 *    UABI_VALIDATE_FD(fd, syscall_num)             — 0 <= fd < MAX_FD
 *    UABI_VALIDATE_POSITIVE(val, syscall_num)      — val > 0
 *    UABI_VALIDATE_RANGE(val, lo, hi, syscall_num) — lo <= val <= hi
 *    UABI_VALIDATE_OUTPUT(ret, syscall_num)        — warn if ret < -5
 *
 * All diagnostics are tagged [VALIDATE FAIL] for grep-ability.
 */

#include "../../include/kabi/kabi_v1.h"
#include "../../libc/string.h"
#include "vfs_core.h"  /* MAX_FD */
#include "syscall_dispatch.h" /* REGS_RET */

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  K-ABI Validation (kernel callers — violations are BUGS)    ║
 * ╚══════════════════════════════════════════════════════════════╝ */

/**
 * Validate a pointer is non-NULL. Panics on failure.
 * Usage: KABI_VALIDATE_PTR(stats, "kabi_get_heap_stats");
 */
#define KABI_VALIDATE_PTR(ptr, func_name)                              \
    do {                                                                \
        if (!(ptr)) {                                                   \
            kprint("[VALIDATE FAIL] " func_name ": NULL pointer '" #ptr "'\n"); \
            panic("[VALIDATE] KABI contract violated");                 \
        }                                                               \
    } while (0)

/**
 * Validate a value is within [min, max]. Panics on failure.
 */
#define KABI_VALIDATE_RANGE(val, lo, hi, func_name)                    \
    do {                                                                \
        if ((int)(val) < (int)(lo) || (int)(val) > (int)(hi)) {         \
            kprint("[VALIDATE FAIL] " func_name ": '" #val "' out of range\n"); \
            panic("[VALIDATE] KABI contract violated");                 \
        }                                                               \
    } while (0)

/**
 * Validate a value is non-zero. Panics on failure.
 */
#define KABI_VALIDATE_NONZERO(val, func_name)                          \
    do {                                                                \
        if ((val) == 0) {                                               \
            kprint("[VALIDATE FAIL] " func_name ": '" #val "' is zero\n"); \
            panic("[VALIDATE] KABI contract violated");                 \
        }                                                               \
    } while (0)

/**
 * Validate alignment. Panics on failure.
 */
#define KABI_VALIDATE_ALIGNED(val, align, func_name)                   \
    do {                                                                \
        if ((val) & ((align) - 1)) {                                    \
            kprint("[VALIDATE FAIL] " func_name ": '" #val "' not aligned\n"); \
            panic("[VALIDATE] KABI contract violated");                 \
        }                                                               \
    } while (0)

/* Helper: print a number into a temp buffer for diagnostics */
static inline void _validate_print_num(int n) {
    char buf[16];
    int_to_ascii(n, buf);
    kprint(buf);
}

/* ╔══════════════════════════════════════════════════════════════╗
 * ║  U-ABI Validation (user callers — never crash the kernel)   ║
 * ╚══════════════════════════════════════════════════════════════╝ */

/**
 * Validate a userspace pointer is non-NULL.
 * On failure: prints diagnostic, sets eax to error, continues to end of handler.
 * Must be used inside syscall_handler where `regs` is available.
 */
#define UABI_VALIDATE_PTR(ptr, syscall_num)                            \
    do {                                                                \
        if (!(ptr)) {                                                   \
            kprint("[VALIDATE FAIL] syscall ");                         \
            _validate_print_num(syscall_num);                           \
            kprint(": NULL pointer '" #ptr "'\n");                      \
            REGS_RET(regs) = (uintptr_t)(-1);                                 \
            goto syscall_done;                                          \
        }                                                               \
    } while (0)

/**
 * Validate a file descriptor is in valid range [0, MAX_FD).
 */
#define UABI_VALIDATE_FD(fd, syscall_num)                              \
    do {                                                                \
        if ((int)(fd) < 0 || (int)(fd) >= MAX_FD) {                     \
            kprint("[VALIDATE FAIL] syscall ");                         \
            _validate_print_num(syscall_num);                           \
            kprint(": fd "); _validate_print_num(fd);                   \
            kprint(" out of range [0,");                                \
            _validate_print_num(MAX_FD);                                \
            kprint(")\n");                                              \
            REGS_RET(regs) = (uintptr_t)(-1);                                 \
            goto syscall_done;                                          \
        }                                                               \
    } while (0)

/**
 * Validate a value is positive (> 0).
 */
#define UABI_VALIDATE_POSITIVE(val, syscall_num)                       \
    do {                                                                \
        if ((int)(val) <= 0) {                                          \
            kprint("[VALIDATE FAIL] syscall ");                         \
            _validate_print_num(syscall_num);                           \
            kprint(": '" #val "' must be > 0 (got ");                   \
            _validate_print_num((int)(val));                            \
            kprint(")\n");                                              \
            REGS_RET(regs) = (uintptr_t)(-1);                                 \
            goto syscall_done;                                          \
        }                                                               \
    } while (0)

/**
 * Validate a value is in range [min, max].
 */
#define UABI_VALIDATE_RANGE(val, lo, hi, syscall_num)                  \
    do {                                                                \
        if ((int)(val) < (int)(lo) || (int)(val) > (int)(hi)) {         \
            kprint("[VALIDATE FAIL] syscall ");                         \
            _validate_print_num(syscall_num);                           \
            kprint(": '" #val "' out of range\n");                      \
            REGS_RET(regs) = (uintptr_t)(-1);                                 \
            goto syscall_done;                                          \
        }                                                               \
    } while (0)

/**
 * Output validation: verify a syscall return value is sane.
 * If the return is an unexpected impossible value, log it loudly.
 */
#define UABI_VALIDATE_OUTPUT(result, syscall_num)                      \
    do {                                                                \
        int _r = (int)(result);                                         \
        if (_r < -5) {                                                  \
            kprint("[VALIDATE WARN] syscall ");                         \
            _validate_print_num(syscall_num);                           \
            kprint(": unexpected return value ");                        \
            _validate_print_num(_r);                                    \
            kprint("\n");                                               \
        }                                                               \
    } while (0)

/**
 * KABI output validation: verify a K-ABI return meets postconditions.
 */
#define KABI_VALIDATE_OUTPUT(result, expected, func_name)               \
    do {                                                                \
        if ((int)(result) != (int)(expected)) {                         \
            kprint("[VALIDATE FAIL] " func_name ": postcondition failed, got "); \
            _validate_print_num((int)(result));                         \
            kprint("\n");                                               \
            panic("[VALIDATE] KABI postcondition violated");            \
        }                                                               \
    } while (0)

#endif /* ABI_VALIDATE_H */
