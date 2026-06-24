#include "mem.h"
#include "../kernel/cpu/paging.h"
#include "../kernel/core/kernel.h"
#include <spinlock.h>

static spinlock_t kmalloc_int_lock = SPINLOCK_INIT;

void memory_copy(uint8_t *source, uint8_t *dest, size_t nbytes) {
    int i;
    for (i = 0; i < nbytes; i++) {
        *(dest + i) = *(source + i);
    }
}

int memory_compare(uint8_t *s1, uint8_t *s2, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return s1[i] - s2[i];
    }
    return 0;
}

void memory_set(uint8_t *dest, uint8_t val, size_t len) {
    uint8_t *temp = (uint8_t *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

#ifndef ARCH_X86_64
uintptr_t free_mem_addr = 0x100000;
#else
extern uint8_t _kernel_end[];
uintptr_t free_mem_addr = (uintptr_t)_kernel_end;
#endif
/* Implementation of Kernel Heap */
#include "kheap.h"

heap_t *kheap = 0;

uint64_t kmalloc_int(size_t size, int align, phys_addr_t *phys_addr) {
    uint64_t f = spin_lock_irqsave(&kmalloc_int_lock);
    
    if (pmm_is_ready) {
        if (size > 0x1000) panic("kmalloc_int: size > 4K requested after PMM ready");
        uint32_t frame = pmm_first_free();
        if (frame == (uint32_t)-1) panic("kmalloc_int: out of physical memory");
        pmm_set_frame(frame);
        uintptr_t ret = (uintptr_t)frame * 0x1000;
        if (phys_addr) *phys_addr = ret;
        spin_unlock_irqrestore(&kmalloc_int_lock, f);
        return (uint64_t)ret;
    }

    /* Pages are aligned to 4K, or 0x1000 */
    if (align == 1 && (free_mem_addr & 0xFFF)) {
        free_mem_addr &= ~0xFFFULL;
        free_mem_addr += 0x1000;
    }
    
    /* Save also the physical address */
    if (phys_addr) *phys_addr = (phys_addr_t)free_mem_addr;

    uintptr_t ret = free_mem_addr;
    free_mem_addr += size; /* Remember to increment the pointer */

    spin_unlock_irqrestore(&kmalloc_int_lock, f);
    return (uint64_t)ret;
}

void *kmalloc(size_t size, int align, phys_addr_t *phys_addr) {
    if (kheap != 0) {
        // kprint("  - [KMem] kmalloc via heap\n");
        void *addr = alloc(size, (uint8_t)align, kheap);
        if (phys_addr) {
            page_t *page = get_page((virt_addr_t)addr, 0, kernel_directory);
#ifndef ARCH_X86_64
            *phys_addr = (phys_addr_t)PAGE_FRAME(page) + ((uintptr_t)addr & 0xFFF);
#else
            *phys_addr = (phys_addr_t)PAGE_FRAME(*page) + ((uintptr_t)addr & 0xFFF);
#endif
        }
        return addr;
    } else {
        return (void*)(uintptr_t)kmalloc_int(size, align, phys_addr);
    }
}

void kfree(void *p) {
    free(p, kheap);
}
