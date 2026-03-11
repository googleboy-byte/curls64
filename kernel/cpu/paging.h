#ifndef PAGING_H
#define PAGING_H

#include "isr.h"
#include <stdint.h>
#include <kernel/arch_types.h>

#define PHYSMAP_BASE 0xE0000000

/* Page Page Entry
 * The page table entries look like this:
 * Bit 0: Present: 1 = page is present in memory
 * Bit 1: RW: 0 = Read-only, 1 = Read/Write
 * Bit 2: User: 0 = Supervisor, 1 = User
 * Bits 12-31: Frame address (shifted right 12 bits)
 */
typedef struct {
   uint32_t present    : 1;   // 0: Page present in memory
   uint32_t rw         : 1;   // 1: Read-only if clear, readwrite if set
   uint32_t user       : 1;   // 2: Supervisor level only if clear
   uint32_t pwt        : 1;   // 3: Page-level write-through
   uint32_t pcd        : 1;   // 4: Page-level cache disable
   uint32_t accessed   : 1;   // 5: Has the page been accessed? (Hardware sets this)
   uint32_t dirty      : 1;   // 6: Has the page been written to? (Hardware sets this)
   uint32_t pat        : 1;   // 7: Page attribute table index
   uint32_t global     : 1;   // 8: Global page (ignored)
   uint32_t cow        : 1;   // 9: Copy-on-Write bit (Software/AVL)
   uint32_t unused     : 2;   // 10-11: Available for system use
   uint32_t frame      : 20;  // 12-31: Frame address (shifted right 12 bits)
} page_t;

typedef struct {
   page_t pages[1024];
} page_table_t;

typedef struct {
   /* Array of pointers to pagetables. */
   page_table_t *tables[1024];
   /* Array of pointers to the pagetables above, but gives their *physical*
    * location, for loading into the CR3 register. */
   uint32_t tablesPhysical[1024];

   /* The physical address of tablesPhysical. This comes into play
    * when we get our kernel heap allocated and the directory
    * may be in a different location in virtual memory. */
   phys_addr_t physicalAddr;
} page_directory_t;

/**
 * Sets up the environment, page directories etc and
 * enables paging.
 */
void init_paging();

/**
 * Load the page directory into the CR3 register.
 */
void switch_page_directory(page_directory_t *new);

/**
 * Retrieve the specific page entry for a given address.
 * If make == 1, create the table if it's missing.
 */
page_t *get_page(virt_addr_t address, int make, page_directory_t *dir);

/**
 * Callback for page fault
 */
void page_fault(registers_t *regs);

void unmap_page(virt_addr_t address);
extern void copy_page_physical(phys_addr_t src, phys_addr_t dest);

/**
 * Creates a duplicate of the given page directory.
 */
page_directory_t *clone_page_directory(page_directory_t *src);
void free_page_directory(page_directory_t *dir);
void promote_to_user_table(page_directory_t *dir, virt_addr_t start_address, uint32_t size);

typedef struct {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t free_frames;
} pmm_stats_t;

void get_pmm_stats(pmm_stats_t *stats);

/* PMM Helpers */
uint32_t pmm_first_free();
void pmm_set_frame(uint32_t frame);
void pmm_clear_frame(uint32_t frame);
int pmm_test_frame(uint32_t frame);

extern void frame_add_ref(uint32_t frame);
void frame_remove_ref(uint32_t frame);
uint8_t frame_get_ref(uint32_t frame);

extern page_directory_t *kernel_directory;
extern int pmm_is_ready;
void pmm_reserve_early_memory();

#endif
