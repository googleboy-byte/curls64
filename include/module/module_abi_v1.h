/**
 * Curls OS Module ABI v1.0: Unified Extension Surface
 * 
 * Welcome to the Curls OS module development ecosystem. This header serves 
 * as the primary entry point for all Curls OS module development. It acts 
 * as the "Sacred Layer" that connects your extensions to the Curls Core.
 * 
 * It is organized into two distinct sections:
 * 
 * SECTION 1: KERNEL-SPACE (K-ABI)
 * - Target: Ring 0 Modules (Drivers, Schedulers, FS).
 * - Access: Direct function calls via the K-ABI bridge.
 * - Privilege: High. Can access hardware and manage memory directly.
 * - Helper Library: libc/ (string.h, mem.h, kheap.h)
 *     Provides kernel-space string operations (int_to_ascii, hex_to_ascii,
 *     strcmp, strcpy, etc.), memory utilities (memory_copy, memory_set),
 *     and heap management (kmalloc, kfree).
 * 
 * SECTION 2: USER-SPACE (U-ABI)
 * - Target: Ring 3 Applications (Shell, Utilities).
 * - Access: System Calls via int 0x80.
 * - Privilege: Restricted. Isolated by the paging system.
 * - Helper Library: user/lib/ulib.h
 *     Provides user-space string operations (ulib_strlen, ulib_strcmp,
 *     ulib_strcpy, ulib_strcat), memory utilities (ulib_memcpy, ulib_memset),
 *     conversion helpers (ulib_int_to_str, ulib_str_to_int), and
 *     convenience I/O wrappers (ulib_print, ulib_gotoxy).
 * 
 * DESIGN PRINCIPLES:
 * 1. Consistency: Shared types and error codes.
 * 2. Separation of Concerns: Clear boundaries between Ring 0 and Ring 3.
 * 3. Discoverability: One-stop shop for developers.
 * 
 * GETTING STARTED:
 * 
 * [Creating a Driver]
 * Drivers should focus on hardware abstraction.
 * 1. Define a global __kabi_module of type kabi_module_t.
 * 2. Implement the init() function.
 * 3. Use kmalloc for your data structures.
 * 4. Register your device using kabi_vfs_register or appropriate K-ABI hooks.
 * 5. Maintain core invariants: never trust user-space pointers without validation.
 * 6. Use libc/ helpers (string.h, mem.h) for common kernel-space operations.
 * 
 * [Creating an Application]
 * Applications should focus on logic and user interaction.
 * 1. Use uabi_open, uabi_read, etc., for file operations.
 * 2. Use uabi_fork and uabi_exec for process management.
 * 3. Adhere to the syscall boundary; the kernel will validate all inputs.
 * 4. Use ulib.h helpers for string operations, memory utilities, and I/O.
 * 
 * VERSIONING & STABILITY:
 * Module ABI v1.0 is committed to binary compatibility within the v1.x series. 
 * Any breaking changes will trigger a bump to v2.0.
 */

#ifndef MODULE_ABI_V1_H
#define MODULE_ABI_V1_H

#include "../kabi/kabi_v1.h"
#ifdef ARCH_X86_64
#include "../uabi/uabi_v2.h"
#else
#include "../uabi/uabi_v1.h"
#endif

// ============================================================================
// SECTION 1: KERNEL-SPACE INTERFACES (K-ABI)
// ============================================================================
/**
 * Interfaces provided by Section 1 are intended for modules running in 
 * Ring 0 (Kernel Mode). Key Pillars:
 * - Direct Hardware Access (via K-ABI hardware abstractions)
 * - Memory Management (kmalloc/kfree)
 * - VFS Registration
 * 
 * Helper Library: libc/
 *   #include "libc/string.h"  — String operations for kernel code
 *   #include "libc/mem.h"     — memory_copy, memory_set
 *   #include "libc/kheap.h"   — Heap internals (advanced use only)
 */

/**
 * MODULE REGISTRATION (Required for all kernel-space modules)
 * 
 * Every K-ABI module must define a global __kabi_module struct.
 * The kernel reads this to verify compatibility and call init().
 */
typedef struct {
    const char *name;           // "usb_driver", "sched_priority", etc.
    uint16_t abi_version;       // Must be MODULE_ABI_V1_0
    uint16_t module_version;    // Your module's version (e.g., 0x0100)
    int (*init)(void);          // Called at module load. Return 0 on success.
    void (*exit)(void);         // Called at module unload (optional, can be NULL)
    const char *description;    // "USB mass storage driver", etc.
} kabi_module_t;

#define MODULE_ABI_V1_0 0x0100

// ============================================================================
// SECTION 2: USER-SPACE INTERFACES (U-ABI)
// ============================================================================
/**
 * Interfaces provided by Section 2 use the system call boundary (int 0x80) 
 * to access kernel services from Ring 3. Key Pillars:
 * - POSIX-like File I/O
 * - Process Control (fork/exec)
 * - IPC (Pipes, Signals)
 * 
 * Helper Library: user/lib/ulib.h
 *   #include "user/lib/ulib.h"  — String, memory, conversion, and I/O helpers
 */

#endif // MODULE_ABI_V1_H
