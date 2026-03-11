#include "paging.h"
#include "isr.h"
#include "../modules/drivers/screen.h"
#include "../core/boot_info.h"
#include "../../libc/mem.h"
#include "../../libc/kheap.h"
#include "../../libc/string.h"
#include "../core/kernel.h"
#include "../core/task.h"

/* The kernel's page directory */
#ifndef ARCH_X86_64
page_directory_t *kernel_directory = 0;
#else
mmu_context_t *kernel_directory = 0;
#endif

/* Current page directory */
#ifndef ARCH_X86_64
page_directory_t *current_directory = 0;
#else
mmu_context_t *current_directory = 0;
#endif

/* Frame reference counting */
/* TODO: Eventually tie this into a formal Physical Memory Manager (PMM) */
#define MAX_FRAMES 32768 // 128MB / 4KB
uint8_t frame_ref_count[MAX_FRAMES];
uint32_t frame_bitmap[MAX_FRAMES / 32];
int pmm_is_ready = 0;

void pmm_set_frame(uint32_t frame) {
    if (frame >= MAX_FRAMES) return;
    frame_bitmap[frame / 32] |= (1 << (frame % 32));
}

void pmm_clear_frame(uint32_t frame) {
    frame_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

int pmm_test_frame(uint32_t frame) {
    if (frame >= MAX_FRAMES) return -1;
    return (frame_bitmap[frame / 32] & (1 << (frame % 32))) ? 1 : 0;
}

uint32_t pmm_first_free() {
    extern uint32_t free_mem_addr;
    uint32_t kernel_limit_frame = (free_mem_addr + 0xFFF) / 0x1000;

    for (uint32_t i = 0; i < MAX_FRAMES / 32; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (!(frame_bitmap[i] & (1 << j))) {
                    // SAFETY: Never return a frame that is in the kernel's early managed range
                    if (frame < kernel_limit_frame) {
                        // This frame should have been reserved. Set it now and continue.
                        pmm_set_frame(frame);
                        continue;
                    }
                    return frame;
                }
            }
        }
    }
    return (uint32_t)-1;
}

void frame_add_ref(uint32_t frame) {
    if (frame < MAX_FRAMES) {
        if (frame_ref_count[frame] == 0) {
            pmm_set_frame(frame);
        }
        frame_ref_count[frame]++;
    }
}

void frame_remove_ref(uint32_t frame) {
    if (frame < MAX_FRAMES && frame_ref_count[frame] > 0) {
        frame_ref_count[frame]--;
        if (frame_ref_count[frame] == 0) {
            pmm_clear_frame(frame);
        }
    }
}

uint8_t frame_get_ref(uint32_t frame) {
    if (frame < MAX_FRAMES) {
        return frame_ref_count[frame];
    }
    return 0;
}

void get_pmm_stats(pmm_stats_t *stats) {
    if (!stats) return;
    uint32_t used = 0;
    uint32_t i = 0;
    while (i < MAX_FRAMES) {
        if (frame_bitmap[i/32] & (1 << (i % 32))) used++;
        i++;
    }
    stats->used_frames = used;
    stats->total_frames = MAX_FRAMES;
    stats->free_frames = stats->total_frames - stats->used_frames;
}

void pmm_reserve_early_memory() {
    extern uint32_t free_mem_addr;
    phys_addr_t end = (free_mem_addr + 0xFFF) & ~0xFFFULL;
    kprint("  - PMM Reserving kernel memory: 0x0 to 0x");
    char s[16]; hex_to_ascii(end, s); kprint(s); kprint("\n");
    for (phys_addr_t i = 0; i < end; i += 0x1000) {
        pmm_set_frame(i / 0x1000);
    }
}

void pmm_init_from_mmap() {
    kprint("  - PMM Initializing from Multiboot2 memory map...\n");
    if (boot_mmap_info.count == 0) {
        kprint("    WARNING: No memory map found, assuming 128MB\n");
        return;
    }

    uint32_t usable_count = 0;
    for (uint32_t i = 0; i < boot_mmap_info.count; i++) {
        if (boot_mmap_info.entries[i].type == 1) { // Usable RAM
            usable_count++;
            // We could mark these as available in the bitmap, 
            // but the current PMM starts with everything "available" (0)
            // and we set bits for "used".
        } else {
            // Reserve non-usable regions
            phys_addr_t start = boot_mmap_info.entries[i].addr;
            phys_addr_t end = start + boot_mmap_info.entries[i].len;
            for (phys_addr_t p = (start & ~0xFFFULL); p < end; p += 0x1000) {
                pmm_set_frame(p / 0x1000);
            }
        }
    }
}

/* Defined in kheap.c/mem.c */
extern uint32_t free_mem_addr;
// extern uint32_t kmalloc(size_t size, int align, uint32_t *phys_addr); // ALREADY IN mem.h


#ifndef ARCH_X86_64
void init_paging() {
    /* The size of physical memory. For the moment we assume 128MB */
    uint32_t mem_end_page = 0x8000000; // 128MB
    
    kprint("  - Initializing Physical Memory Manager (Bitmap)...\n");
    memory_set((uint8_t*)frame_bitmap, 0, sizeof(frame_bitmap));
    memory_set((uint8_t*)frame_ref_count, 0, sizeof(frame_ref_count));

    pmm_init_from_mmap();
    kprint("  - Finalizing PMM Reservation (Kernel)...\n");
    pmm_reserve_early_memory();
    pmm_is_ready = 1;

    kprint("  - Creating Linear PHYSMAP at 0xE0000000...\n");
    for (i = 0; i < mem_end_page; i += 0x1000) {
        page_t *page = get_page(PHYSMAP_BASE + i, 1, kernel_directory);
        page->present = 1;
        page->rw = 1;
        page->user = 0;
        page->frame = i / 0x1000;
    }

    kprint("  - Mapping kernel heap at 0xC0000000...\n");
    // Pre-allocate all PDEs for the kernel heap range (up to 0xD0000000)
    // This ensures that all processes (which clone these PDEs) see the same page tables.
    // If we don't do this, expanding the heap into a new PDE range won't be visible
    // to existing processes.
    for (i = KHEAP_START; i < 0xD0000000; i += 0x400000) {
        get_page(i, 1, kernel_directory);
    }

    // Map the initial heap pages
    for (i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += 0x1000) {
        page_t *page = get_page(i, 1, kernel_directory);
        page->present = 1;
        page->rw = 1;
        page->user = 0;
        uint32_t phys_addr;
        kmalloc_int(0x1000, 1, &phys_addr); 
        page->frame = phys_addr / 0x1000;
    }

    /* CRITICAL: Now that ALL bootstrap allocations are done, reserve all 
       frames from physical 0 up to the current free_mem_addr in the PMM.
       This protects kernel code, initrd, and the page tables we just built. */
    for (uint32_t f = 0; f < free_mem_addr; f += 0x1000) {
        pmm_set_frame(f / 0x1000);
        frame_ref_count[f / 0x1000] = 1;
    }

    kprint("  - Registering page fault handler...\n");
    register_interrupt_handler(14, page_fault);

    kprint("  - Enabling paging (switching CR3)...\n");
    switch_page_directory(kernel_directory);

    kprint("  - Initializing kernel heap structure...\n");
    kheap = create_heap(KHEAP_START, KHEAP_START + KHEAP_INITIAL_SIZE, 0xCFFFF000, 0, 0);
    
    page_t *p = get_page((uint32_t)kheap, 0, kernel_directory);
    kprint("  - KHEAP structure at Phys: ");
    if (p) {
        char s[16]; hex_to_ascii(p->frame * 0x1000, s); kprint(s);
    } else kprint("NONE");
    kprint("\n");

    kprint("  - Paging and Heap ready.\n");

    // Map framebuffer (if provided by bootloader) at FB_VIRT_BASE.
    // We rely on this for the linear framebuffer driver instead of
    // identity-mapping arbitrary high physical addresses.
    if (boot_fb_info.present &&
        boot_fb_info.type == 1 &&
        (boot_fb_info.bpp == 24 || boot_fb_info.bpp == 32)) {

        uint64_t fb_start = boot_fb_info.addr & ~0xFFFULL;
        uint64_t fb_size  = (uint64_t)boot_fb_info.pitch * (uint64_t)boot_fb_info.height;
        uint64_t fb_end   = fb_start + fb_size + 0x1000; // include partial last page

        uint64_t phys = fb_start;
        uint32_t virt = FB_VIRT_BASE;

        while (phys < fb_end) {
            page_t *page = get_page(virt, 1, kernel_directory);
            page->present = 1;
            page->rw      = 1;
            page->user    = 0;
            page->frame   = (uint32_t)(phys / 0x1000);

            phys += 0x1000;
            virt += 0x1000;
        }
    }
}

void switch_page_directory(page_directory_t *dir) {
    current_directory = dir;
    asm volatile("mov %0, %%cr3":: "r"(dir->physicalAddr));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0": "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    cr0 |= 0x10000;    // Enable Write Protect (WP) bit!
    asm volatile("mov %0, %%cr0":: "r"(cr0));
}

page_t *get_page(uint32_t address, int make, page_directory_t *dir) {
    uint32_t f = irq_save();
    /* Turn the address into an index. */
    address /= 0x1000;
    /* Find the page table containing this address. */
    uint32_t table_idx = address / 1024;

    if (dir->tables[table_idx]) { // If this table is already assigned
        irq_restore(f);
        return &dir->tables[table_idx]->pages[address%1024];
    } else if(make) {
        uint32_t tmp;
        dir->tables[table_idx] = (page_table_t*)kmalloc(sizeof(page_table_t), 1, &tmp);
        memory_set((uint8_t*)dir->tables[table_idx], 0, 0x1000); // clear memory
        dir->tablesPhysical[table_idx] = tmp | 0x7; // PRESENT, RW, USER.
        irq_restore(f);
        return &dir->tables[table_idx]->pages[address%1024];
    } else {
        irq_restore(f);
        return 0;
    }
}

void unmap_page(uint32_t address) {
    page_t *page = get_page(address, 0, kernel_directory);
    if (page) {
        page->present = 0;
        /* Invalidate the TLB for this address */
        asm volatile("invlpg (%0)" ::"r" (address) : "memory");
    }
}

void page_fault(registers_t *regs) {
    /* A page fault has occurred. */
    /* The faulting address is stored in the CR2 register. */
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    /* The error code gives us details of what happened. */
    int not_present = !(regs->err_code & 0x1); // Page not present
    int protection_violation = regs->err_code & 0x1; // Page present, but access denied
    int rw = regs->err_code & 0x2;           // Write operation?
    int us = regs->err_code & 0x4;           // Processor was in user-mode?
    int reserved = regs->err_code & 0x8;     // Overwritten CPU-reserved bits of page entry?
    int id = regs->err_code & 0x10;          // Caused by an instruction fetch?
    char s[16];

    /* Analyze the fault */
    if (protection_violation && rw) {
        // Potential COW fault
        page_t *page = get_page(faulting_address, 0, current_directory);
        if (page && page->cow && page->user) {
            uint32_t old_frame = page->frame;
            if (frame_get_ref(old_frame) > 1) {
                // COW: Allocate new frame and copy via PHYSMAP
                uint32_t new_frame = pmm_first_free();
                if (new_frame == (uint32_t)-1) panic("COW: Out of physical memory");
                uint32_t new_phys = new_frame * 0x1000;
                
                // Use PHYSMAP for safe kernel access to physical memory
                memory_copy((uint8_t*)(PHYSMAP_BASE + old_frame * 0x1000), 
                            (uint8_t*)(PHYSMAP_BASE + new_phys), 0x1000);
                
                frame_remove_ref(old_frame);
                page->frame = new_phys / 0x1000;
                frame_add_ref(page->frame);
                page->cow = 0;
                page->rw = 1;
            } else {
                // Last reference, just make it writable
                page->cow = 0;
                page->rw = 1;
            }
            // Flush TLB
            asm volatile("invlpg (%0)" ::"r" (faulting_address) : "memory");
            return; // SILENT SUCCESS FOR COW
        }
    }

    // If we reach here, it's a REAL fault. Print details.
    kprint("\nPage Fault ( ");
    if (not_present) kprint("not-present ");
    if (protection_violation) kprint("protection-violation ");
    if (rw) kprint("write "); else kprint("read ");
    if (us) kprint("user-mode ");
    if (reserved) kprint("reserved ");
    if (id) kprint("instruction-fetch ");
    kprint(") at 0x"); hex_to_ascii(faulting_address, s); kprint(s);
    kprint(" EIP: 0x"); hex_to_ascii(regs->eip, s); kprint(s);
    kprint("\n");

    // Diagnostic: Print PDE/PTE bits
    uint32_t table_idx = faulting_address / 0x400000;
    uint32_t page_idx = (faulting_address / 0x1000) % 1024;
    kprint("  - PDE["); int_to_ascii(table_idx, s); kprint(s); kprint("]: 0x");
    hex_to_ascii(current_directory->tablesPhysical[table_idx], s); kprint(s);
    kprint("\n");
    if (current_directory->tables[table_idx]) {
        page_t *p = &current_directory->tables[table_idx]->pages[page_idx];
        uint32_t *p_raw = (uint32_t*)p;
        kprint("  - PTE["); int_to_ascii(page_idx, s); kprint(s); kprint("]: 0x");
        hex_to_ascii(*p_raw, s); kprint(s);
        kprint(" (P:"); int_to_ascii(p->present, s); kprint(s);
        kprint(" R/W:"); int_to_ascii(p->rw, s); kprint(s);
        kprint(" U/S:"); int_to_ascii(p->user, s); kprint(s);
        kprint(" COW:"); int_to_ascii(p->cow, s); kprint(s);
        kprint(")\n");
    }

    if (us) {
        // Mark as zombie and switch away
        task_t *self = (task_t*)current_task;
        self->state = TASK_ZOMBIE;
        
        // Wake up parent
        if (self->parent && self->parent->state == TASK_WAITING) {
            self->parent->state = TASK_READY;
        }

        if (irq_depth > 0) irq_depth--;
        asm volatile("sti; hlt");
        while(1);
    } else {
        if (not_present) {
             if (faulting_address == 0) {
                 kprint("KERNEL NULL DEREF at 0x0\n");
             } else if (faulting_address < 0x1000) {
                 kprint("KERNEL NULL DEREF (Near NULL)\n");
             } else {
                 kprint("PAGE FAULT (NOT PRESENT)\n");
             }
        } 
        
        kprint("Kernel PANIC! System Halted.\n");
        asm("hlt");
    }
}

static page_table_t *clone_table(page_table_t *src, uint32_t *physAddr, int cow_enabled) {
    uint32_t f = irq_save();
    page_table_t *table = (page_table_t*)kmalloc(sizeof(page_table_t), 1, physAddr);
    if (!table) panic("clone_table: Out of memory for page table");
    memory_set((uint8_t*)table, 0, sizeof(page_table_t));

    for (int i = 0; i < 1024; i++) {
        if (src->pages[i].frame) {
            // Shallow copy: link to the same frame
            table->pages[i].frame = src->pages[i].frame;
            table->pages[i].present = src->pages[i].present;
            table->pages[i].user = src->pages[i].user;
            table->pages[i].accessed = src->pages[i].accessed;
            table->pages[i].dirty = src->pages[i].dirty;

            // COW: Only participate if it's a USER page AND COW is enabled.
            if (cow_enabled && src->pages[i].user && src->pages[i].rw) {
                src->pages[i].rw = 0;
                src->pages[i].cow = 1;
                table->pages[i].rw = 0;
                table->pages[i].cow = 1;
            } else {
                table->pages[i].rw = src->pages[i].rw;
                table->pages[i].cow = src->pages[i].cow;
            }
            
            frame_add_ref(table->pages[i].frame);
        }
    }
    irq_restore(f);
    return table;
}

void promote_to_user_table(page_directory_t *dir, uint32_t start_address, uint32_t size) {
    uint32_t end_address = start_address + size;
    
    // Iterate over all page tables involved in this range
    for (uint32_t addr = start_address & 0xFFFFF000; addr < end_address; ) {
        uint32_t table_idx = addr / (1024 * 0x1000);
        uint32_t table_end = (table_idx + 1) * (1024 * 0x1000);
        
        if (dir->tables[table_idx]) {
            // Check if this table is currently shared with the kernel
            if (dir->tables[table_idx] == kernel_directory->tables[table_idx]) {
                // Table is shared. We MUST clone it to make it private and user-accessible.
                // WE NEVER COW KERNEL TABLES.
                uint32_t phys;
                dir->tables[table_idx] = clone_table(kernel_directory->tables[table_idx], &phys, 0);
                dir->tablesPhysical[table_idx] = phys | 0x07; // Present, RW, USER
            }
            
            // Now mark all pages in the requested range within THIS table as USER
            uint32_t inner_start = (addr > start_address) ? addr : start_address;
            uint32_t inner_end = (table_end < end_address) ? table_end : end_address;
            
            for (uint32_t p_addr = inner_start & 0xFFFFF000; p_addr < inner_end; p_addr += 0x1000) {
                page_t *page = &dir->tables[table_idx]->pages[(p_addr / 0x1000) % 1024];
                if (page->frame) {
                    page->user = 1;
                    page->rw = 1;  // Ensure it's writable if code/stack
                    page->cow = 0; // Ensure it's not COW
                }
            }
        }
        
        addr = table_end;
    }
}

page_directory_t *clone_page_directory(page_directory_t *src) {
    uint32_t f = irq_save();
    uint32_t phys;
    page_directory_t *dir = (page_directory_t*)kmalloc(sizeof(page_directory_t), 1, &phys);
    if (!dir) panic("clone_page_directory: Out of memory for page directory");
    memory_set((uint8_t*)dir, 0, sizeof(page_directory_t));

    // Get the physical address of the tablesPhysical array
    uint32_t offset = (uint32_t)dir->tablesPhysical - (uint32_t)dir;
    dir->physicalAddr = phys + offset;

    for (int i = 0; i < 1024; i++) {
        if (!src->tables[i]) continue;

        if (kernel_directory->tables[i] == src->tables[i]) {
            // Kernel table: just link
            dir->tables[i] = src->tables[i];
            dir->tablesPhysical[i] = src->tablesPhysical[i];
        } else {
            // User table: clone it with COW
            uint32_t table_phys;
            dir->tables[i] = clone_table(src->tables[i], &table_phys, 1);
            dir->tablesPhysical[i] = table_phys | 0x07;
        }
    }

    // Flush TLB to ensure COW write-protection takes effect immediately
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax");

    return dir;
}

void free_page_directory(page_directory_t *dir) {
    if (!dir || dir == kernel_directory) return;

    for (int i = 0; i < 1024; i++) {
        if (!dir->tables[i]) continue;

        // If it's a kernel table, DO NOT free it or its pages
        if (dir->tables[i] == kernel_directory->tables[i]) continue;

        // User table: Free all its pages (release frames)
        for (int j = 0; j < 1024; j++) {
            if (dir->tables[i]->pages[j].frame) {
                frame_remove_ref(dir->tables[i]->pages[j].frame);
            }
        }
        // Free the table itself
        kfree(dir->tables[i]);
    }
    // Free the directory itself
    kfree(dir);
}
#endif
