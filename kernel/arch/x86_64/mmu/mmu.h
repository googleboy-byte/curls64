#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <kernel/arch_types.h>

/**
 * @file mmu.h
 * @brief x86_64 4-level Paging (MMU) implementation.
 */

// Paging entry bits
#define MMU_PRESENT    (1ULL << 0)
#define MMU_WRITABLE   (1ULL << 1)
#define MMU_USER       (1ULL << 2)
#define MMU_PWT        (1ULL << 3)
#define MMU_PCD        (1ULL << 4)
#define MMU_ACCESSED   (1ULL << 5)
#define MMU_DIRTY      (1ULL << 6)
#define MMU_HUGE       (1ULL << 7)
#define MMU_GLOBAL     (1ULL << 8)
#define MMU_COW        (1ULL << 9)   // Software bit (AVL)
#define MMU_NX         (1ULL << 63)  // No-execute

typedef uint64_t mmu_entry_t;

typedef struct {
    mmu_entry_t entries[512];
} mmu_table_t;

typedef struct {
    phys_addr_t pml4_phys;
    mmu_table_t *pml4_virt;
} mmu_context_t;

/**
 * @brief Initialize the 64-bit MMU.
 */
void mmu_init(void);

/**
 * @brief Map a physical page to a virtual address.
 */
int mmu_map_page(mmu_context_t *ctx, virt_addr_t virt, phys_addr_t phys, uint64_t flags);

/**
 * @brief Get the paging entry for a virtual address.
 */
mmu_entry_t *mmu_get_entry(mmu_context_t *ctx, virt_addr_t virt);

/**
 * @brief Unmap a virtual page.
 */
void mmu_unmap_page(mmu_context_t *ctx, virt_addr_t virt);

/**
 * @brief Clone a user address space (COW).
 */
mmu_context_t *mmu_clone_user(mmu_context_t *src);

/**
 * @brief Switch the current address space (load CR3).
 */
void mmu_switch(mmu_context_t *ctx);

/**
 * @brief Flag to indicate if higher-half mapping (PHYSMAP) is active.
 */
extern int mmu_high_active;

/**
 * @brief Invalidate a single TLB entry.
 */
static inline void mmu_invlpg(virt_addr_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

#endif // MMU_H
