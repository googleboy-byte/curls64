#include "mmu.h"
#include "../../../cpu/paging.h"
#include "../../../../libc/mem.h"
#include "../../../../libc/string.h"
#include "../../../core/kernel.h"
#include "../../../modules/drivers/screen.h"

/**
 * @file mmu.c
 * @brief Implementation of x86_64 4-level paging.
 */

// Indices for each level
#define PML4_IDX(v) (((v) >> 39) & 0x1FF)
#define PDPT_IDX(v) (((v) >> 30) & 0x1FF)
#define PD_IDX(v)   (((v) >> 21) & 0x1FF)
#define PT_IDX(v)   (((v) >> 12) & 0x1FF)

// Helper to get a virtual pointer to a physical page frame
// Using the PHYSMAP concept from Phase 0
static inline void* phys_to_virt(phys_addr_t phys) {
    return (void*)((uintptr_t)PHYSMAP_BASE + (uintptr_t)phys);
}

static mmu_table_t* get_or_alloc_table(mmu_table_t *parent, int index, uint64_t flags) {
    if (!(parent->entries[index] & MMU_PRESENT)) {
        phys_addr_t phys;
        mmu_table_t *new_table = (mmu_table_t*)kmalloc(sizeof(mmu_table_t), 1, &phys);
        if (!new_table) return NULL;
        
        memory_set((uint8_t*)new_table, 0, sizeof(mmu_table_t));
        // Tables are always Present|Writable|User at the directory level to allow restricted leaf entries
        parent->entries[index] = phys | MMU_PRESENT | MMU_WRITABLE | MMU_USER;
        return new_table;
    }
    return (mmu_table_t*)phys_to_virt(parent->entries[index] & ~0xFFFULL);
}

mmu_entry_t *mmu_get_entry(mmu_context_t *ctx, virt_addr_t virt) {
    mmu_table_t *pml4 = ctx->pml4_virt;
    
    if (!(pml4->entries[PML4_IDX(virt)] & MMU_PRESENT)) return NULL;
    mmu_table_t *pdpt = phys_to_virt(pml4->entries[PML4_IDX(virt)] & ~0xFFFULL);
    
    if (!(pdpt->entries[PDPT_IDX(virt)] & MMU_PRESENT)) return NULL;
    mmu_table_t *pd = phys_to_virt(pdpt->entries[PDPT_IDX(virt)] & ~0xFFFULL);
    
    if (!(pd->entries[PD_IDX(virt)] & MMU_PRESENT)) return NULL;
    mmu_table_t *pt = phys_to_virt(pd->entries[PD_IDX(virt)] & ~0xFFFULL);
    
    return &pt->entries[PT_IDX(virt)];
}

int mmu_map_page(mmu_context_t *ctx, virt_addr_t virt, phys_addr_t phys, uint64_t flags) {
    mmu_table_t *pml4 = ctx->pml4_virt;
    
    mmu_table_t *pdpt = get_or_alloc_table(pml4, PML4_IDX(virt), flags);
    if (!pdpt) return -1;
    
    mmu_table_t *pd = get_or_alloc_table(pdpt, PDPT_IDX(virt), flags);
    if (!pd) return -1;
    
    mmu_table_t *pt = get_or_alloc_table(pd, PD_IDX(virt), flags);
    if (!pt) return -1;
    
    pt->entries[PT_IDX(virt)] = (phys & ~0xFFFULL) | flags | MMU_PRESENT;
    return 0;
}

void mmu_unmap_page(mmu_context_t *ctx, virt_addr_t virt) {
    mmu_table_t *pml4 = ctx->pml4_virt;
    
    if (!(pml4->entries[PML4_IDX(virt)] & MMU_PRESENT)) return;
    mmu_table_t *pdpt = phys_to_virt(pml4->entries[PML4_IDX(virt)] & ~0xFFFULL);
    
    if (!(pdpt->entries[PDPT_IDX(virt)] & MMU_PRESENT)) return;
    mmu_table_t *pd = phys_to_virt(pdpt->entries[PDPT_IDX(virt)] & ~0xFFFULL);
    
    if (!(pd->entries[PD_IDX(virt)] & MMU_PRESENT)) return;
    mmu_table_t *pt = phys_to_virt(pd->entries[PD_IDX(virt)] & ~0xFFFULL);
    
    pt->entries[PT_IDX(virt)] = 0;
    mmu_invlpg(virt);
}

void mmu_switch(mmu_context_t *ctx) {
    asm volatile("mov %0, %%cr3" : : "r"(ctx->pml4_phys) : "memory");
}

static mmu_table_t* clone_table(mmu_table_t *src, uint64_t *physAddr, int cow_enabled) {
    phys_addr_t phys;
    mmu_table_t *table = (mmu_table_t*)kmalloc(sizeof(mmu_table_t), 1, &phys);
    if (!table) return NULL;
    *physAddr = phys;
    memory_set((uint8_t*)table, 0, sizeof(mmu_table_t));

    for (int i = 0; i < 512; i++) {
        if (src->entries[i] & MMU_PRESENT) {
            // Shallow copy + reference count
            table->entries[i] = src->entries[i];
            
            if (cow_enabled && (src->entries[i] & MMU_USER) && (src->entries[i] & MMU_WRITABLE)) {
                src->entries[i] &= ~MMU_WRITABLE;
                src->entries[i] |= MMU_COW;
                table->entries[i] &= ~MMU_WRITABLE;
                table->entries[i] |= MMU_COW;
            }
            
            frame_add_ref(table->entries[i] / 0x1000);
        }
    }
    return table;
}

mmu_context_t *mmu_clone_user(mmu_context_t *src) {
    phys_addr_t phys;
    mmu_table_t *new_pml4 = (mmu_table_t*)kmalloc(sizeof(mmu_table_t), 1, &phys);
    if (!new_pml4) return NULL;
    memory_set((uint8_t*)new_pml4, 0, sizeof(mmu_table_t));

    mmu_context_t *ctx = (mmu_context_t*)kmalloc(sizeof(mmu_context_t), 0, NULL);
    ctx->pml4_phys = phys;
    ctx->pml4_virt = new_pml4;

    for (int i = 0; i < 512; i++) {
        if (!(src->pml4_virt->entries[i] & MMU_PRESENT)) continue;

        // If it's a kernel range (higher-half or identity low), just share it
        // For now we assume anything < 256 is user, >= 256 is kernel (simple split)
        if (i >= 256) {
            new_pml4->entries[i] = src->pml4_virt->entries[i];
            // We don't refcount kernel tables typically as they are persistent
        } else {
            // Clone user PDPT
            phys_addr_t pdpt_phys;
            mmu_table_t *src_pdpt = phys_to_virt(src->pml4_virt->entries[i] & ~0xFFFULL);
            mmu_table_t *new_pdpt = clone_table(src_pdpt, &pdpt_phys, 1);
            new_pml4->entries[i] = pdpt_phys | MMU_PRESENT | MMU_WRITABLE | MMU_USER;
        }
    }

    return ctx;
}

void mmu_init(void) {
    // Initial x64 boot-time identity map is already set up by ASM trampoline.
    // We will eventually replace it with a clean kernel address space here.
    kprint("  - x86_64 MMU implementation active\n");
}

/* Compatibility wrapper for get_page used by common code (e.g. kmalloc) */
page_t *get_page(virt_addr_t address, int make, page_directory_t *dir) {
    mmu_entry_t *entry = mmu_get_entry(dir, address);
    if (entry) return entry;
    
    if (make) {
        // Map with default flags (Present|Writable|Supervisor)
        // We don't have a physical frame here, so we map to 0 and let the 
        // caller set the frame later (standard Curls pattern).
        if (mmu_map_page(dir, address, 0, MMU_WRITABLE) == 0) {
            return mmu_get_entry(dir, address);
        }
    }
    return NULL;
}
