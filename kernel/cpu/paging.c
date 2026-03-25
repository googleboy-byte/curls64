#include "paging.h"
#include <cpu_local.h>
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
/* x86_64: Dynamically sized from Multiboot2 memory map */
uint8_t *frame_ref_count = 0;
uint32_t *frame_bitmap = 0;
uint64_t total_frames = 0;
int pmm_is_ready = 0;

#include <spinlock.h>
static spinlock_t pmm_lock = SPINLOCK_INIT;

void pmm_set_frame(uint32_t frame) {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    if (!frame_bitmap || frame >= total_frames) { spin_unlock_irqrestore(&pmm_lock, flags); return; }
    frame_bitmap[frame / 32] |= (1 << (frame % 32));
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_clear_frame(uint32_t frame) {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    if (!frame_bitmap || frame >= total_frames) { spin_unlock_irqrestore(&pmm_lock, flags); return; }
    frame_bitmap[frame / 32] &= ~(1 << (frame % 32));
    spin_unlock_irqrestore(&pmm_lock, flags);
}

int pmm_test_frame(uint32_t frame) {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    if (!frame_bitmap || frame >= total_frames) { spin_unlock_irqrestore(&pmm_lock, flags); return -1; }
    int res = (frame_bitmap[frame / 32] & (1 << (frame % 32))) ? 1 : 0;
    spin_unlock_irqrestore(&pmm_lock, flags);
    return res;
}

uint32_t pmm_first_free() {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    if (!frame_bitmap) { spin_unlock_irqrestore(&pmm_lock, flags); return (uint32_t)-1; }
    uintptr_t kernel_limit_frame = (free_mem_addr + 0xFFF) / 0x1000;

    for (uint32_t i = 0; i < total_frames / 32; i++) {
        if (frame_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t frame = i * 32 + j;
                if (!(frame_bitmap[i] & (1 << j))) {
                    if (frame >= total_frames) break;
                    // SAFETY: Never return a frame that is in the kernel's early managed range
                    if (frame < kernel_limit_frame) {
                        // This frame should have been reserved. Set it now and continue.
                        frame_bitmap[frame / 32] |= (1 << (frame % 32)); // inline to avoid deadlock
                        continue;
                    }
                    spin_unlock_irqrestore(&pmm_lock, flags);
                    return frame;
                }
            }
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
    return (uint32_t)-1;
}

void frame_add_ref(uint32_t frame) {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    if (frame_ref_count && frame < total_frames) {
        if (frame_ref_count[frame] == 0) {
            frame_bitmap[frame / 32] |= (1 << (frame % 32)); // inline to avoid deadlock
        }
        frame_ref_count[frame]++;
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void frame_remove_ref(uint32_t frame) {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    if (frame_ref_count && frame < total_frames && frame_ref_count[frame] > 0) {
        frame_ref_count[frame]--;
        if (frame_ref_count[frame] == 0) {
            frame_bitmap[frame / 32] &= ~(1 << (frame % 32)); // inline to avoid deadlock
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

uint8_t frame_get_ref(uint32_t frame) {
    uint64_t flags = spin_lock_irqsave(&pmm_lock);
    uint8_t ref = 0;
    if (frame_ref_count && frame < total_frames) {
        ref = frame_ref_count[frame];
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
    return ref;
}

void get_pmm_stats(pmm_stats_t *stats) {
    if (!stats || !frame_bitmap) return;
    uint32_t used = 0;
    for (uint32_t i = 0; i < total_frames; i++) {
        if (pmm_test_frame(i) == 1) used++;
    }
    stats->used_frames = used;
    stats->total_frames = total_frames;
    stats->free_frames = stats->total_frames - stats->used_frames;
}

void pmm_reserve_early_memory() {
    phys_addr_t end = (free_mem_addr + 0xFFF) & ~0xFFFULL;
    kprint("  - PMM Reserving kernel memory: 0x0 to ");
    char s[20]; hex64_to_ascii(end, s); kprint(s); kprint("\n");
    for (phys_addr_t i = 0; i < end; i += 0x1000) {
        pmm_set_frame(i / 0x1000);
    }
}

#define PMM_OOM_SAFE_RESERVE 512

int pmm_test_oom(void) {
    /* Step 1: Count free frames */
    pmm_stats_t stats;
    get_pmm_stats(&stats);
    uint32_t free_count = (uint32_t)stats.free_frames;

    if (free_count <= PMM_OOM_SAFE_RESERVE) {
        kprint("[PMM OOM TEST] Already low on frames, skipping\n");
        return -1;
    }

    /* Step 2: Allocate (free_count - SAFE_RESERVE) frames */
    uint32_t to_alloc = free_count - PMM_OOM_SAFE_RESERVE;
    uint32_t *allocated = (uint32_t*)kmalloc(
        (size_t)to_alloc * sizeof(uint32_t), 0, 0);
    if (!allocated) {
        kprint("[PMM OOM TEST] kmalloc failed for tracking array\n");
        return -1;
    }

    uint32_t count = 0;
    while (count < to_alloc) {
        uint32_t f = pmm_first_free();
        if (f == (uint32_t)-1) break;
        pmm_set_frame(f);
        allocated[count++] = f;
    }

    /* Step 3: Verify near-OOM state */
    pmm_stats_t after;
    get_pmm_stats(&after);
    int near_oom = ((uint32_t)after.free_frames <= PMM_OOM_SAFE_RESERVE);

    /* Step 4: Free ALL allocated frames immediately */
    for (uint32_t i = 0; i < count; i++) {
        pmm_clear_frame(allocated[i]);
    }
    kfree(allocated);

    /* Step 5: Verify recovery */
    pmm_stats_t recovered;
    get_pmm_stats(&recovered);
    int recovered_ok = ((uint32_t)recovered.free_frames >= free_count - 10);

    char s2[20];
    kprint("[PMM OOM TEST] allocated=");
    int_to_ascii(count, s2); kprint(s2);
    kprint(" near_oom=");
    int_to_ascii(near_oom, s2); kprint(s2);
    kprint(" recovered_frames=");
    int_to_ascii((int)recovered.free_frames, s2); kprint(s2);
    kprint("\n");

    return (near_oom && recovered_ok) ? 1 : 0;
}

void pmm_init_from_mmap() {
    kprint("  - PMM Initializing from Multiboot2 memory map...\n");
    uint64_t max_phys = 0x8000000; // 128MB fallback
    if (boot_mmap_info.count > 0) {
        max_phys = 0;
        for (uint32_t i = 0; i < boot_mmap_info.count; i++) {
            uint64_t end = boot_mmap_info.entries[i].addr + boot_mmap_info.entries[i].len;
            if (end > max_phys) max_phys = end;
        }
    }

    total_frames = max_phys / 0x1000;
    kprint("    Total RAM detected: ");
    char s[20]; hex64_to_ascii(max_phys, s); kprint(s); kprint(" (");
    char s2[20]; hex64_to_ascii(total_frames, s2); kprint(s2); kprint(" frames)\n");

    // Dynamic allocation of PMM structures
    frame_bitmap = (uint32_t*)kmalloc((size_t)((total_frames / 32 + 1) * 4), 1, NULL);
    frame_ref_count = (uint8_t*)kmalloc((size_t)total_frames, 1, NULL);
    memory_set((uint8_t*)frame_bitmap, 0, (size_t)((total_frames / 32 + 1) * 4));
    memory_set((uint8_t*)frame_ref_count, 0, (size_t)total_frames);

    if (boot_mmap_info.count == 0) {
        kprint("    WARNING: No memory map found, using fallback\n");
        return;
    }

    for (uint32_t i = 0; i < boot_mmap_info.count; i++) {
        if (boot_mmap_info.entries[i].type != 1) { // NOT Usable RAM
            phys_addr_t start = boot_mmap_info.entries[i].addr;
            phys_addr_t end = start + boot_mmap_info.entries[i].len;
            for (phys_addr_t p = (start & ~0xFFFULL); p < end; p += 0x1000) {
                pmm_set_frame((uint32_t)(p / 0x1000));
            }
        }
    }
}

extern uintptr_t free_mem_addr;

#ifdef ARCH_X86_64
void init_paging() {
    kprint("Initializing 64-bit Paging (4-level MMU)...\n");

    // Allocate PML4 for kernel
    phys_addr_t pml4_phys;
    kernel_directory = (mmu_context_t*)kmalloc(sizeof(mmu_context_t), 1, NULL);
    kernel_directory->pml4_virt = (mmu_table_t*)kmalloc(sizeof(mmu_table_t), 1, &pml4_phys);
    kernel_directory->pml4_phys = pml4_phys;
    memory_set((uint8_t*)kernel_directory->pml4_virt, 0, sizeof(mmu_table_t));

    kprint("  - Initializing Physical Memory Manager...\n");
    pmm_init_from_mmap();
    pmm_reserve_early_memory();
    pmm_is_ready = 1;

    kprint("  - Registering page fault handler...\n");
    register_interrupt_handler(14, page_fault);

    // 1. Identity map the low 64MB
    kprint("  - Identity mapping low 64MB...\n");
    for (uint64_t i = 0; i < 0x4000000; i += 0x1000) {
        mmu_map_page(kernel_directory, i, i, MMU_WRITABLE);
    }

    // 2. Map PHYSMAP
    kprint("  - Mapping PHYSMAP to ");
    char s[20]; hex64_to_ascii(PHYSMAP_BASE, s); kprint(s); kprint("\n");
    uint64_t max_phys = 0x8000000; // 128MB default
    if (boot_mmap_info.count > 0) {
        for (uint32_t i = 0; i < boot_mmap_info.count; i++) {
            uint64_t end = boot_mmap_info.entries[i].addr + boot_mmap_info.entries[i].len;
            if (end > max_phys) max_phys = end;
        }
    }
    for (uint64_t i = 0; i < max_phys; i += 0x1000) {
        mmu_map_page(kernel_directory, PHYSMAP_BASE + i, i, MMU_WRITABLE);
    }

    // 3. Map Kernel Heap initial range
    kprint("  - Mapping Kernel Heap to ");
    hex64_to_ascii(KHEAP_START, s); kprint(s); kprint("\n");
    for (uint64_t i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += 0x1000) {
        uint32_t frame = pmm_first_free();
        if (frame == (uint32_t)-1) panic("init_paging: out of physical memory for kheap");
        pmm_set_frame(frame);
        phys_addr_t phys = (phys_addr_t)frame * 0x1000;
        mmu_map_page(kernel_directory, i, phys, MMU_WRITABLE);
    }

    // 4. Map Framebuffer if present
    if (boot_fb_info.present) {
        kprint("  - Mapping Framebuffer to ");
        hex64_to_ascii(FB_VIRT_BASE, s); kprint(s); kprint("\n");
        uint64_t fb_phys_start = boot_fb_info.addr & ~0xFFFULL;
        uint64_t offset = boot_fb_info.addr & 0xFFF;
        uint64_t fb_size = (uint64_t)boot_fb_info.pitch * (uint64_t)boot_fb_info.height + offset;
        for (uint64_t i = 0; i < fb_size; i += 0x1000) {
            mmu_map_page(kernel_directory, FB_VIRT_BASE + i, fb_phys_start + i, MMU_WRITABLE);
        }
    }

    kprint("  - Switching to 64-bit kernel context (CR0.WP enforcement)...\n");
    mmu_switch(kernel_directory);
    
    // Enforce Write Protect (WP) bit in CR0
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16); 
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    mmu_high_active = 1;
    kernel_directory->pml4_virt = (mmu_table_t*)((uintptr_t)kernel_directory->pml4_virt + PHYSMAP_BASE);
    kernel_directory = (mmu_context_t*)((uintptr_t)kernel_directory + PHYSMAP_BASE);
    current_directory = kernel_directory;

    kprint("  - Initializing kernel heap structure...\n");
    kheap = create_heap(KHEAP_START, KHEAP_START + KHEAP_INITIAL_SIZE, KHEAP_MAX_ADDR, 0, 0);
    kheap = (heap_t*)((uintptr_t)kheap + PHYSMAP_BASE);
    frame_bitmap = (uint32_t*)((uintptr_t)frame_bitmap + PHYSMAP_BASE);
    frame_ref_count = (uint8_t*)((uintptr_t)frame_ref_count + PHYSMAP_BASE);

    kprint("  - 64-bit Paging and Heap ready.\n");
}
#else
void init_paging() {
    uint32_t i;
    uint32_t mem_end_page = 0x8000000;
    pmm_init_from_mmap();
    pmm_reserve_early_memory();
    pmm_is_ready = 1;

    for (i = 0; i < mem_end_page; i += 0x1000) {
        page_t *page = get_page(PHYSMAP_BASE + i, 1, kernel_directory);
        page->present = 1; page->rw = 1; page->user = 0;
        page->frame = i / 0x1000;
    }

    for (i = KHEAP_START; i < 0xD0000000; i += 0x400000) {
        get_page(i, 1, kernel_directory);
    }

    for (i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += 0x1000) {
        page_t *page = get_page(i, 1, kernel_directory);
        page->present = 1; page->rw = 1; page->user = 0;
        uint32_t phys_addr;
        kmalloc_int(0x1000, 1, &phys_addr); 
        page->frame = phys_addr / 0x1000;
    }

    for (uint32_t f = 0; f < free_mem_addr; f += 0x1000) {
        pmm_set_frame(f / 0x1000);
        frame_ref_count[f / 0x1000] = 1;
    }

    register_interrupt_handler(14, page_fault);
    switch_page_directory(kernel_directory);
    kheap = create_heap(KHEAP_START, KHEAP_START + KHEAP_INITIAL_SIZE, 0xCFFFF000, 0, 0);
}
#endif

void page_fault(registers_t *regs) {
    virt_addr_t faulting_address = read_cr2();

    int not_present = !(regs->err_code & 0x1);
    int protection_violation = regs->err_code & 0x1;
    int rw = regs->err_code & 0x2;
    int us = regs->err_code & 0x4;
    int reserved = regs->err_code & 0x8;
    int id = regs->err_code & 0x10;
    char s[32];

    if (protection_violation && rw) {
        page_t *page = get_page(faulting_address, 0, current_directory);
#ifdef ARCH_X86_64
        if (page && (*page & MMU_COW) && (*page & MMU_USER)) {
            uintptr_t old_frame = PAGE_FRAME(*page);
#else
        if (page && page->cow && page->user) {
            uint32_t old_frame = page->frame * 0x1000;
#endif
            if (frame_get_ref(old_frame / 0x1000) > 1) {
                uint32_t new_frame = pmm_first_free();
                if (new_frame == (uint32_t)-1) {
                    char _s[32];
                    kprint("[MM] OOM: COW fault pid ");
                    int_to_ascii(((task_t*)current_task)->id, _s); kprint(_s);
                    kprint(" addr 0x"); hex64_to_ascii(faulting_address, _s); kprint(_s);
                    kprint(" -- sending SIGKILL\n");
                    task_deliver_signal((task_t*)current_task, SIGKILL);
                    return;
                }
                uintptr_t new_phys = (uintptr_t)new_frame * 0x1000;
                memory_copy((uint8_t*)(PHYSMAP_BASE + old_frame), (uint8_t*)(PHYSMAP_BASE + new_phys), 0x1000);
                frame_remove_ref(old_frame / 0x1000);
#ifdef ARCH_X86_64
                PAGE_SET_FRAME(page, new_phys);
                *page &= ~MMU_COW;
                *page |= MMU_WRITABLE;
#else
                page->frame = new_phys / 0x1000;
                page->cow = 0; page->rw = 1;
#endif
                frame_add_ref(new_frame);
            } else {
#ifdef ARCH_X86_64
                *page &= ~MMU_COW;
                *page |= MMU_WRITABLE;
#else
                page->cow = 0; page->rw = 1;
#endif
            }
            asm volatile("invlpg (%0)" ::"r" (faulting_address) : "memory");
            return;
        }
    }

    kprint("\nPage Fault ( ");
    if (not_present) kprint("not-present ");
    if (protection_violation) kprint("protection-violation ");
    if (rw) kprint("write "); else kprint("read ");
    if (us) kprint("user-mode ");
    if (reserved) kprint("reserved ");
    if (id) kprint("instruction-fetch ");
    
    kprint(") at 0x"); hex64_to_ascii(faulting_address, s); kprint(s);
#ifdef ARCH_X86_64
    kprint(" RIP: 0x"); hex64_to_ascii(regs->rip, s); kprint(s);
#else
    kprint(" EIP: 0x"); hex_to_ascii(regs->eip, s); kprint(s);
#endif
    kprint("\n");
    if (us) {
#ifdef ARCH_X86_64
        kprint("PF INSTR at 0x"); hex64_to_ascii(regs->rip, s); kprint(s); kprint(": ");
        uint8_t *instr = (uint8_t*)regs->rip;
        for (int i = 0; i < 4; i++) {
            hex_to_ascii(instr[i], s); kprint(s); kprint(" ");
        }
        kprint("\n");
        char s2[20];
        kprint("PF REGS: rax="); hex64_to_ascii(regs->rax, s2); kprint(s2);
        kprint(" rbx="); hex64_to_ascii(regs->rbx, s2); kprint(s2);
        kprint(" rcx="); hex64_to_ascii(regs->rcx, s2); kprint(s2);
        kprint(" rdx="); hex64_to_ascii(regs->rdx, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: rsi="); hex64_to_ascii(regs->rsi, s2); kprint(s2);
        kprint(" rdi="); hex64_to_ascii(regs->rdi, s2); kprint(s2);
        kprint(" rbp="); hex64_to_ascii(regs->rbp, s2); kprint(s2);
        kprint(" rsp="); hex64_to_ascii(regs->rsp, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: r8 ="); hex64_to_ascii(regs->r8, s2); kprint(s2);
        kprint(" r9 ="); hex64_to_ascii(regs->r9, s2); kprint(s2);
        kprint(" r10="); hex64_to_ascii(regs->r10, s2); kprint(s2);
        kprint(" r11="); hex64_to_ascii(regs->r11, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: r12="); hex64_to_ascii(regs->r12, s2); kprint(s2);
        kprint(" r13="); hex64_to_ascii(regs->r13, s2); kprint(s2);
        kprint(" r14="); hex64_to_ascii(regs->r14, s2); kprint(s2);
        kprint(" r15="); hex64_to_ascii(regs->r15, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: rip="); hex64_to_ascii(regs->rip, s2); kprint(s2);
        kprint(" cs ="); hex64_to_ascii(regs->cs, s2); kprint(s2);
        kprint(" rfl="); hex64_to_ascii(regs->rflags, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: ursp="); hex64_to_ascii(regs->rsp, s2); kprint(s2);
        kprint(" ss="); hex64_to_ascii(regs->ss, s2); kprint(s2); kprint("\n");
#else
        kprint("PF INSTR at 0x"); hex_to_ascii(regs->eip, s); kprint(s); kprint(": ");
        uint8_t *instr = (uint8_t*)regs->eip;
        for (int i = 0; i < 4; i++) {
            hex_to_ascii(instr[i], s); kprint(s); kprint(" ");
        }
        kprint("\n");
        char s2[20];
        kprint("PF REGS: eax="); hex_to_ascii(regs->eax, s2); kprint(s2);
        kprint(" ebx="); hex_to_ascii(regs->ebx, s2); kprint(s2);
        kprint(" ecx="); hex_to_ascii(regs->ecx, s2); kprint(s2);
        kprint(" edx="); hex_to_ascii(regs->edx, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: esi="); hex_to_ascii(regs->esi, s2); kprint(s2);
        kprint(" edi="); hex_to_ascii(regs->edi, s2); kprint(s2);
        kprint(" ebp="); hex_to_ascii(regs->ebp, s2); kprint(s2);
        kprint(" esp="); hex_to_ascii(regs->esp, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: eip="); hex_to_ascii(regs->eip, s2); kprint(s2);
        kprint(" cs ="); hex_to_ascii(regs->cs, s2); kprint(s2);
        kprint(" efl="); hex_to_ascii(regs->eflags, s2); kprint(s2); kprint("\n");
        kprint("PF REGS: useresp="); hex_to_ascii(regs->esp, s2); kprint(s2);
        kprint(" ss="); hex_to_ascii(regs->ss, s2); kprint(s2); kprint("\n");
#endif

        task_t *self = (task_t*)current_task;
        self->state = TASK_ZOMBIE;
        if (self->parent && self->parent->state == TASK_WAITING) self->parent->state = TASK_READY;
        irq_depth = 0;
        asm volatile("sti; hlt");
        while(1);
    } else {
        panic("Kernel PAGE FAULT");
    }
}

#ifndef ARCH_X86_64
void switch_page_directory(page_directory_t *dir) {
    current_directory = (page_directory_t*)dir;
    asm volatile("mov %0, %%cr3":: "r"(dir->physicalAddr));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0": "=r"(cr0));
    cr0 |= 0x80000000; cr0 |= 0x10000;
    asm volatile("mov %0, %%cr0":: "r"(cr0));
}

page_t *get_page(uint32_t address, int make, page_directory_t *dir) {
    address /= 0x1000;
    uint32_t table_idx = address / 1024;
    if (dir->tables[table_idx]) {
        return &dir->tables[table_idx]->pages[address%1024];
    } else if(make) {
        uint32_t tmp;
        dir->tables[table_idx] = (page_table_t*)kmalloc(sizeof(page_table_t), 1, &tmp);
        memory_set((uint8_t*)dir->tables[table_idx], 0, 0x1000);
        dir->tablesPhysical[table_idx] = tmp | 0x7;
        return &dir->tables[table_idx]->pages[address%1024];
    }
    return 0;
}

void unmap_page(uint32_t address) {
    page_t *page = get_page(address, 0, (page_directory_t*)kernel_directory);
    if (page) { page->present = 0; asm volatile("invlpg (%0)" ::"r" (address) : "memory"); }
}

#ifndef ARCH_X86_64
boot_mmap_info_t boot_mmap_info = {0};

page_directory_t *clone_page_directory(page_directory_t *src) {
    return src;
}

void free_page_directory(page_directory_t *dir) {
    (void)dir;
}

void promote_to_user_table(page_directory_t *dir, virt_addr_t virt, uint32_t size) {
    (void)dir; (void)virt; (void)size;
}
#endif
#endif
