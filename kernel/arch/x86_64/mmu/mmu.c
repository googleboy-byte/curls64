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

// Flag to indicate if higher-half mapping (PHYSMAP) is active
int mmu_high_active = 0;
extern mmu_context_t *current_directory;

// Helper to get a virtual pointer to a physical page frame
// Using the PHYSMAP concept from Phase 0
static inline void* phys_to_virt(phys_addr_t phys) {
    if (!mmu_high_active) {
        // During bootstrap, we rely on identity mapping (16MB)
        return (void*)((uintptr_t)phys);
    }
    return (void*)((uintptr_t)PHYSMAP_BASE + (uintptr_t)phys);
}

static mmu_table_t* alloc_table(phys_addr_t *out_phys) {
    uint32_t frame = pmm_first_free();
    if (frame == (uint32_t)-1) panic("mmu: out of physical memory for page table");
    pmm_set_frame(frame);
    phys_addr_t phys = (phys_addr_t)frame * 0x1000;
    if (out_phys) *out_phys = phys;
    mmu_table_t *table = (mmu_table_t*)phys_to_virt(phys);
    memory_set((uint8_t*)table, 0, sizeof(mmu_table_t));
    return table;
}

static void free_page_table(mmu_table_t *table) {
    if (!table) return;
    uint64_t phys = (uintptr_t)table - PHYSMAP_BASE;
    frame_remove_ref((uint32_t)(phys / 0x1000));
}

static mmu_table_t* get_or_alloc_table(mmu_table_t *parent, int index, uint64_t flags) {
    if (!(parent->entries[index] & MMU_PRESENT)) {
        phys_addr_t phys;
        mmu_table_t *new_table = alloc_table(&phys);
        if (!new_table) return NULL;
        
        // Tables are always Present|Writable|User at the directory level to allow restricted leaf entries
        parent->entries[index] = phys | MMU_PRESENT | MMU_WRITABLE | MMU_USER;
        return new_table;
    }
    
    // If table exists but we need User access, ensure the directory entry has it
    if (flags & MMU_USER) {
        parent->entries[index] |= MMU_USER;
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
    mmu_invlpg(virt);
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

static mmu_table_t* clone_table(mmu_table_t *src, uint64_t *physAddr, int level, int cow_enabled) {
    phys_addr_t phys;
    mmu_table_t *table = alloc_table(&phys);
    if (!table) return NULL;
    *physAddr = phys;

    for (int i = 0; i < 512; i++) {
        if (!(src->entries[i] & MMU_PRESENT)) continue;

        if (level < 3) {
            // Internal table level (PML4, PDPT, PD)
            uint64_t child_phys;
            mmu_table_t *src_child = (mmu_table_t*)phys_to_virt(src->entries[i] & ~0xFFFULL);
            mmu_table_t *dst_child = clone_table(src_child, &child_phys, level + 1, cow_enabled);
            table->entries[i] = child_phys | (src->entries[i] & 0xFFF);
        } else {
            // Leaf level (PT)
            table->entries[i] = src->entries[i];
            
            if (cow_enabled && (src->entries[i] & MMU_USER) && (src->entries[i] & MMU_WRITABLE)) {
                src->entries[i] &= ~MMU_WRITABLE;
                src->entries[i] |= MMU_COW;
                table->entries[i] &= ~MMU_WRITABLE;
                table->entries[i] |= MMU_COW;
            }
            
            frame_add_ref((uint64_t)((table->entries[i] & ~0xFFFULL) / 0x1000));
        }
    }
    return table;
}

static void free_table(mmu_table_t *table, int level) {
    if (!table) return;

    for (int i = 0; i < 512; i++) {
        if (!(table->entries[i] & MMU_PRESENT)) continue;

        if (level < 3) {
            // Recurse into child table
            mmu_table_t *child = (mmu_table_t*)phys_to_virt(table->entries[i] & ~0xFFFULL);
            free_table(child, level + 1);
        } else {
            // Leaf level: decrement frame refcount
            uint64_t frame = (table->entries[i] & ~0xFFFULL) / 0x1000;
            frame_remove_ref((uint32_t)frame);
        }
    }
    free_page_table(table);
}

mmu_context_t *mmu_clone_user(mmu_context_t *src) {
    phys_addr_t phys;
    mmu_table_t *new_pml4 = alloc_table(&phys);
    if (!new_pml4) return NULL;

    mmu_context_t *ctx = (mmu_context_t*)kmalloc(sizeof(mmu_context_t), 0, NULL);
    ctx->pml4_phys = phys;
    ctx->pml4_virt = new_pml4;

    for (int i = 0; i < 512; i++) {
        if (!(src->pml4_virt->entries[i] & MMU_PRESENT)) continue;

        if (i >= 256) {
            // Kernel higher-half: share directly
            new_pml4->entries[i] = src->pml4_virt->entries[i];
        } else if (i == 0) {
            // PML4[0] contains both the kernel identity map (0-128MB) AND user space.
            // We must surgically share the kernel part and COW-clone the user part.
            uint64_t pdpt_phys;
            mmu_table_t *src_pdpt = (mmu_table_t*)phys_to_virt(src->pml4_virt->entries[0] & ~0xFFFULL);
            mmu_table_t *new_pdpt = alloc_table((phys_addr_t*)&pdpt_phys);

            for (int j = 0; j < 512; j++) {
                if (!(src_pdpt->entries[j] & MMU_PRESENT)) continue;

                if (j == 0) {
                    // PDPT[0] contains PDs.
                    // PD indices 0-63 cover 0MB to 128MB (kernel identity map).
                    // PD indices 64-511 cover user space.
                    uint64_t pd_phys;
                    mmu_table_t *src_pd = (mmu_table_t*)phys_to_virt(src_pdpt->entries[0] & ~0xFFFULL);
                    mmu_table_t *new_pd = alloc_table((phys_addr_t*)&pd_phys);

                    for (int k = 0; k < 512; k++) {
                        if (!(src_pd->entries[k] & MMU_PRESENT)) continue;
                        if (k < 64) {
                            // Kernel identity map: share directly
                            new_pd->entries[k] = src_pd->entries[k];
                        } else {
                            // User space: COW clone
                            uint64_t pt_phys;
                            mmu_table_t *src_pt = (mmu_table_t*)phys_to_virt(src_pd->entries[k] & ~0xFFFULL);
                            mmu_table_t *new_pt = clone_table(src_pt, &pt_phys, 3, 1);
                            new_pd->entries[k] = pt_phys | (src_pd->entries[k] & 0xFFF);
                        }
                    }
                    new_pdpt->entries[0] = pd_phys | (src_pdpt->entries[0] & 0xFFF);
                } else {
                    // Other PDPT entries in PML4[0] are user space: COW clone
                    uint64_t pd_phys;
                    mmu_table_t *src_pd = (mmu_table_t*)phys_to_virt(src_pdpt->entries[j] & ~0xFFFULL);
                    mmu_table_t *new_pd = clone_table(src_pd, &pd_phys, 2, 1);
                    new_pdpt->entries[j] = pd_phys | (src_pdpt->entries[j] & 0xFFF);
                }
            }
            new_pml4->entries[0] = pdpt_phys | (src->pml4_virt->entries[0] & 0xFFF);
        } else {
            // Other user PML4 entries (1-255): COW clone user PDPT
            uint64_t pdpt_phys;
            mmu_table_t *src_pdpt = (mmu_table_t*)phys_to_virt(src->pml4_virt->entries[i] & ~0xFFFULL);
            mmu_table_t *new_pdpt = clone_table(src_pdpt, &pdpt_phys, 1, 1);
            new_pml4->entries[i] = pdpt_phys | (src->pml4_virt->entries[i] & 0xFFF);
        }
    }

    return ctx;
}

void mmu_init(void) {
    // Initial x64 boot-time identity map is already set up by ASM trampoline.
    // We will eventually replace it with a clean kernel address space here.

    // Enforce Write Protect (WP) bit in CR0
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16); 
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    kprint("  - x86_64 MMU implementation active (CR0.WP enabled)\n");
}

/* Compatibility wrapper for get_page used by common code (e.g. kmalloc) */
page_t *get_page(virt_addr_t address, int make, page_directory_t *dir) {
    mmu_entry_t *entry = mmu_get_entry(dir, address);
    if (entry) return entry;
    
    if (make) {
        // Just create the page table hierarchy without mapping a physical frame.
        // We do this by calling mmu_get_entry's internal logic or a helper.
        // For now, mmu_get_entry doesn't 'make'. 
        // Let's use mmu_map_page with a special flag or just fix the traversal.
        
        // Actually, mmu_map_page with phys=0 sets the frame to 0. 
        // We want a 'present=0' entry but with the table existing.
        // Let's implement a small helper to ensure the hierarchy exists.
        
        mmu_table_t *pml4 = dir->pml4_virt;
        int pml4_idx = (address >> 39) & 0x1FF;
        int pdpt_idx = (address >> 30) & 0x1FF;
        int pd_idx   = (address >> 21) & 0x1FF;
        int pt_idx   = (address >> 12) & 0x1FF;

        mmu_table_t *pdpt = get_or_alloc_table(pml4, pml4_idx, MMU_WRITABLE | MMU_PRESENT | MMU_USER);
        if (!pdpt) return NULL;
        mmu_table_t *pd   = get_or_alloc_table(pdpt, pdpt_idx, MMU_WRITABLE | MMU_PRESENT | MMU_USER);
        if (!pd) return NULL;
        mmu_table_t *pt   = get_or_alloc_table(pd, pd_idx, MMU_WRITABLE | MMU_PRESENT | MMU_USER);
        if (!pt) return NULL;

        return &pt->entries[pt_idx];
    }
    return NULL;
}

void switch_page_directory(page_directory_t *dir) {
    current_directory = dir;
    mmu_switch(dir);
}

void unmap_page(virt_addr_t address) {
    mmu_unmap_page(current_directory, address);
}

page_directory_t *clone_page_directory(page_directory_t *src) {
    return mmu_clone_user(src);
}

void free_page_directory(page_directory_t *dir) {
    if (!dir || dir == kernel_directory) return;
    
    mmu_table_t *pml4 = (mmu_table_t*)dir->pml4_virt;
    if (!pml4) {
        kfree(dir);
        return;
    }

    // Only free user-space half of the tables (0-255).
    // Kernel higher-half (256-511) is shared and should not be freed.
    for (int i = 0; i < 256; i++) {
        if (!(pml4->entries[i] & MMU_PRESENT)) continue;

        if (i == 0) {
            // Surgically clean up PML4[0] (Identity map + User space)
            mmu_table_t *pdpt = (mmu_table_t*)phys_to_virt(pml4->entries[0] & ~0xFFFULL);
            for (int j = 0; j < 512; j++) {
                if (!(pdpt->entries[j] & MMU_PRESENT)) continue;
                
                if (j == 0) {
                    // PDPT[0] contains PDs.
                    // PD indices 0-63 cover 0MB to 128MB (kernel identity map) and are shared.
                    // PD indices 64-511 cover user space and were cloned.
                    mmu_table_t *pd = (mmu_table_t*)phys_to_virt(pdpt->entries[0] & ~0xFFFULL);
                    for (int k = 64; k < 512; k++) {
                        if (pd->entries[k] & MMU_PRESENT) {
                            mmu_table_t *pt = (mmu_table_t*)phys_to_virt(pd->entries[k] & ~0xFFFULL);
                            free_table(pt, 3);
                        }
                    }
                    free_page_table(pd);
                } else {
                    // Other PDPT entries in PML4[0] are entirely user space
                    mmu_table_t *pd = (mmu_table_t*)phys_to_virt(pdpt->entries[j] & ~0xFFFULL);
                    free_table(pd, 2);
                }
            }
            free_page_table(pdpt);
        } else {
            // Other user PML4 entries (1-255)
            mmu_table_t *pdpt = (mmu_table_t*)phys_to_virt(pml4->entries[i] & ~0xFFFULL);
            free_table(pdpt, 1);
        }
    }
    free_page_table(pml4);
    
    kfree(dir);
}

void promote_to_user_table(page_directory_t *dir, virt_addr_t start, uint32_t len) {
    // 64-bit MMU map logic in exec.c already ensures user flags are set 
    // for specific ranges. This is a no-op fallback for now.
}
