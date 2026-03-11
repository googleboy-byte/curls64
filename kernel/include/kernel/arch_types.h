#ifndef ARCH_TYPES_H
#define ARCH_TYPES_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file arch_types.h
 * @brief Architecture-independent address and size types for the Curls kernel.
 * 
 * These types are used to facilitate the porting of the kernel to different
 * architectures (e.g., i386 to x86_64) by providing a consistent set of
 * types for memory addresses and sizes.
 */

#ifdef __x86_64__
    typedef uint64_t virt_addr_t;
    typedef uint64_t phys_addr_t;
#else
    typedef uint32_t virt_addr_t;
    typedef uint32_t phys_addr_t;
#endif

// Ensure size_t is consistently available
// (Already included via <stddef.h>, but explicit mention helps documentation)

#endif // ARCH_TYPES_H
