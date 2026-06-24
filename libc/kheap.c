#include "kheap.h"
#include "../kernel/cpu/paging.h"
#include "mem.h"
#include "string.h"
#include "../kernel/core/task.h"
#include <spinlock.h>

static spinlock_t heap_lock = SPINLOCK_INIT;

/* Lock-free internal free — called from expand() which already holds heap_lock */
static void free_internal(void *p, heap_t *heap);

/* Forward declarations */
int8_t header_t_less_than(void* a, void* b);
ordered_array_t place_ordered_array(void *addr, uint32_t max_size, lessthan_pred_t less_than);
void insert_ordered_array(void *item, ordered_array_t *array);
void* lookup_ordered_array(uint32_t i, ordered_array_t *array);
void remove_ordered_array(uint32_t i, ordered_array_t *array);
extern page_directory_t *kernel_directory;

void expand(virt_addr_t new_size, heap_t *heap);
virt_addr_t contract(virt_addr_t new_size, heap_t *heap);

heap_t *create_heap(virt_addr_t start, virt_addr_t end, virt_addr_t max, uint8_t supervisor, uint8_t readonly) {
    phys_addr_t phys;
    heap_t *heap = (heap_t*)kmalloc_int(sizeof(heap_t), 0, &phys); // Kmalloc_int used for bootstrapping

    /* Assume start address is valid aligned, but index array needs space */
    /* Implementation detail available in tutorials: 
       We put the index array at 'start', then shift 'start' forward 
       However, here we passed start as 0xC0000000. 
       We must ensure that space exists. */
    
    /* Initialize the index array. */
    heap->index = place_ordered_array((void*)start, HEAP_INDEX_SIZE, &header_t_less_than);
    
    /* Shift the start address forward to resemble where we can put data. */
    start += sizeof(type_t)*HEAP_INDEX_SIZE;

    /* Make sure the start address is page-aligned. */
    if ((start & 0xFFF) != 0) {
        start &= ~0xFFFULL;
        start += 0x1000;
    }

    /* Write the start, end and max addresses into the heap structure. */
    heap->start_address = start;
    heap->end_address = end;
    heap->max_address = max;
    heap->supervisor = supervisor;
    heap->readonly = readonly;

    /* We start off with one large hole in the index. */
    header_t *hole = (header_t *)start;
    hole->size = (uint32_t)(end - start);
    hole->magic = HEAP_MAGIC;
    hole->is_hole = 1;
    
    insert_ordered_array((void*)hole, &heap->index);

    return heap;
}

static int32_t find_smallest_hole(size_t size, uint8_t page_align, heap_t *heap) {
    uint32_t iterator = 0;
    while (iterator < heap->index.size) {
        header_t *header = (header_t *)lookup_ordered_array(iterator, &heap->index);
        /* If page aligned, we need to check alignment logic */
        if (page_align > 0) {
            virt_addr_t location = (virt_addr_t)header;
            size_t offset = 0;
            if (((location + sizeof(header_t)) & 0xFFF) != 0)
                offset = 0x1000 - (location + sizeof(header_t)) % 0x1000;
            // Guard against underflow: skip holes too small for alignment
            if (offset >= (size_t)header->size) { iterator++; continue; }
            size_t hole_size = (size_t)header->size - offset;
            /* Can we fit it? */
            if (hole_size >= size) break;
        } else if (header->size >= size) {
            break;
        }
        iterator++;
    }
    if (iterator == heap->index.size)
        return -1; // Not found
    else
        return iterator;
}

void *alloc(size_t size, uint8_t page_align, heap_t *heap) {
    size_t new_size = size + sizeof(header_t) + sizeof(footer_t);
    uint64_t f = spin_lock_irqsave(&heap_lock);
    int32_t iterator = find_smallest_hole(new_size, page_align, heap);

    if (iterator == -1) {
        size_t old_size = (size_t)(heap->end_address - heap->start_address);
        size_t new_heap_size = old_size * 2;
        
        // Ensure new_heap_size is enough for the request
        if (new_heap_size < old_size + size + sizeof(header_t) + sizeof(footer_t)) {
             new_heap_size = old_size + size + sizeof(header_t) + sizeof(footer_t);
        }

        expand(new_heap_size, heap);
        
        // Now that we expanded, we should have a new large hole at the end.
        // We need to re-search for a hole.
        iterator = find_smallest_hole(new_size, page_align, heap);
        
        if (iterator == -1) {
            spin_unlock_irqrestore(&heap_lock, f);
            return 0; // Still failed!
        }
    }

    header_t *orig_hole_header = (header_t *)lookup_ordered_array(iterator, &heap->index);
    uintptr_t orig_hole_pos = (uintptr_t)orig_hole_header;
    uint32_t orig_hole_size = orig_hole_header->size;

    // Sanity check: ensure metadata is not corrupt
    if (orig_hole_size > (heap->max_address - heap->start_address)) {
        panic("Heap allocator: Corrupt hole size detected (metadata smashed)");
    }

    // Always remove from index before modifying
    remove_ordered_array(iterator, &heap->index);

    // Alignment logic
    if (page_align && (orig_hole_pos + sizeof(header_t)) & 0xFFF) {
        virt_addr_t new_pos = (orig_hole_pos + sizeof(header_t) + 0xFFF) & ~0xFFFULL;
        virt_addr_t new_header_pos = new_pos - sizeof(header_t);
        
        uint32_t prefix_size = (uint32_t)(new_header_pos - orig_hole_pos);
        
        if (prefix_size >= sizeof(header_t) + sizeof(footer_t)) {
            // Create prefix hole only if large enough for proper metadata
            header_t *pref_header = (header_t *)orig_hole_pos;
            pref_header->size = prefix_size;
            pref_header->magic = HEAP_MAGIC;
            pref_header->is_hole = 1;
            footer_t *pref_footer = (footer_t *) (new_header_pos - sizeof(footer_t));
            pref_footer->magic = HEAP_MAGIC;
            pref_footer->header = pref_header;

            // Re-insert the prefix hole into the index
            insert_ordered_array((void*)pref_header, &heap->index);
        }
        // If prefix too small, we silently waste that space (no metadata corruption)

        orig_hole_pos = new_header_pos;
        orig_hole_size -= prefix_size;
    }

    // Now check if we can split the remaining space
    // If the space is too small for a meaningful split, just consume it all
    if (orig_hole_size - new_size < sizeof(header_t) + sizeof(footer_t)) {
        size += orig_hole_size - new_size;
        new_size = orig_hole_size;
    }

    /* Overwrite the block header */
    header_t *block_header  = (header_t *)orig_hole_pos;
    block_header->magic     = HEAP_MAGIC;
    block_header->is_hole   = 0;
    block_header->size      = (uint32_t)new_size;
    
    footer_t *block_footer  = (footer_t *) (orig_hole_pos + sizeof(header_t) + size);
    block_footer->magic     = HEAP_MAGIC;
    block_footer->header    = block_header;

    /* If we left a hole, create a new header for it */
    if (orig_hole_size - new_size > 0) {
        header_t *hole_header   = (header_t *) (orig_hole_pos + sizeof(header_t) + size + sizeof(footer_t));
        hole_header->magic      = HEAP_MAGIC;
        hole_header->is_hole    = 1;
        hole_header->size       = orig_hole_size - new_size;
        footer_t *hole_footer   = (footer_t *) ( (uintptr_t)hole_header + hole_header->size - sizeof(footer_t) );
        if ((uintptr_t)hole_footer < heap->end_address) {
             hole_footer->magic = HEAP_MAGIC;
             hole_footer->header = hole_header;
        }
        insert_ordered_array((void*)hole_header, &heap->index);
    }
    
    spin_unlock_irqrestore(&heap_lock, f);
    return (void *) ( (uintptr_t)block_header + sizeof(header_t) );
}

void expand(virt_addr_t new_size, heap_t *heap) {
    virt_addr_t old_size = (virt_addr_t)(heap->end_address - heap->start_address);
    if (new_size <= old_size) return;

    if (new_size & 0xFFF) {
        new_size &= 0xFFFFF000;
        new_size += 0x1000;
    }

    if (heap->start_address + new_size > heap->max_address) {
        panic("Heap expansion exceeded maximum address");
    }

    uint32_t i = old_size;
    while (i < new_size) {
        page_t *page = get_page(heap->start_address + i, 1, kernel_directory);
#ifndef ARCH_X86_64
        page->present = 1;
        page->rw = (heap->readonly) ? 0 : 1;
        page->user = (heap->supervisor) ? 0 : 1;
#else
        uint64_t flags = MMU_PRESENT;
        if (!heap->readonly) flags |= MMU_WRITABLE;
        if (!heap->supervisor) flags |= MMU_USER;
        PAGE_SET_FLAGS(page, flags);
#endif
        
        phys_addr_t phys;
        // Use PMM if ready, otherwise fallback to placement
        extern int pmm_is_ready;
        extern uint32_t pmm_first_free();
        if (pmm_is_ready) {
            uint32_t frame = pmm_first_free();
            if (frame == (uint32_t)-1) panic("Heap expand: Out of physical memory");
            pmm_set_frame(frame);
            phys = (phys_addr_t)frame * 0x1000;
        } else {
            kmalloc_int(0x1000, 1, &phys);
        }
        PAGE_SET_FRAME(page, phys);
        
        i += 0x1000;
    }
    
    virt_addr_t old_end = heap->end_address;
    heap->end_address = heap->start_address + new_size;

    // Create a hole for the new space and free it to add to index
    header_t *hole_header = (header_t *)old_end;
    hole_header->magic = HEAP_MAGIC;
    hole_header->is_hole = 1;
    hole_header->size = (uint32_t)(new_size - (old_end - heap->start_address));

    footer_t *hole_footer = (footer_t *) ( (uintptr_t)hole_header + hole_header->size - sizeof(footer_t) );
    hole_footer->magic = HEAP_MAGIC;
    hole_footer->header = hole_header;

    // Use the internal free (no lock) — caller (alloc) already holds heap_lock
    // free_internal() takes a pointer to the DATA part
    free_internal((void*)((uintptr_t)hole_header + sizeof(header_t)), heap);
}

virt_addr_t contract(virt_addr_t new_size, heap_t *heap) {
    if (new_size & 0xFFF) {
        new_size &= ~0xFFFULL;
        new_size += 0x1000;
    }
    if (new_size < HEAP_MIN_SIZE) new_size = HEAP_MIN_SIZE;
    
    // For now, we'll just update the end address without unmapping frames
    // to keep it simple and safe. Real contraction would require unmapping.
    if (new_size < heap->end_address - heap->start_address) {
         heap->end_address = heap->start_address + new_size;
    }
    
    return (virt_addr_t)(heap->end_address - heap->start_address);
}

/* Lock-free internal free — used by expand() which already holds heap_lock */
static void free_internal(void *p, heap_t *heap) {
    if (p == 0) return;
    header_t *header = (header_t*) ( (uintptr_t)p - sizeof(header_t) );
    footer_t *footer = (footer_t*) ( (uintptr_t)header + header->size - sizeof(footer_t) );

    if (header->magic != HEAP_MAGIC) return; // Sanity check
    if (footer->magic != HEAP_MAGIC) return;

    header->is_hole = 1;

    // Merge right
    header_t *next_header = (header_t *) ((uintptr_t)footer + sizeof(footer_t));
    if ((uintptr_t)next_header >= heap->start_address &&
        (uintptr_t)next_header + sizeof(header_t) <= heap->end_address &&
        next_header->magic == HEAP_MAGIC && next_header->is_hole) {
        header->size += next_header->size;
        // Remove next_header from the index
        uint32_t iterator = 0;
        while (iterator < heap->index.size && lookup_ordered_array(iterator, &heap->index) != (void *)next_header)
            iterator++;
        if (iterator < heap->index.size)
            remove_ordered_array(iterator, &heap->index);
        
        // Update footer after merge
        footer = (footer_t*) ( (uintptr_t)header + header->size - sizeof(footer_t) );
        footer->magic = HEAP_MAGIC;
        footer->header = header;
        
        // DIAGNOSTIC: check if merge-right created hole spanning PID2
        if ((uintptr_t)header <= 0xFFFFA00000104000ULL && (uintptr_t)header + header->size > 0xFFFFA00000104000ULL) {
            char s[32];
            kprint("[HEAP BUG] merge-right created hole spanning PID2!\n");
            kprint("  hole=0x"); hex64_to_ascii((uintptr_t)header, s); kprint(s);
            kprint(" size=0x"); hex64_to_ascii(header->size, s); kprint(s);
            kprint(" freed_p=0x"); hex64_to_ascii((uintptr_t)p, s); kprint(s); kprint("\n");
            panic("HEAP: merge-right spans PID2 stack");
        }
    }

    // Merge left
    footer_t *prev_footer = (footer_t *) ((uintptr_t)header - sizeof(footer_t));
    if ((uintptr_t)prev_footer >= heap->start_address &&
        (uintptr_t)prev_footer + sizeof(footer_t) <= (uintptr_t)header &&
        prev_footer->magic == HEAP_MAGIC &&
        (uintptr_t)prev_footer->header >= heap->start_address &&
        (uintptr_t)prev_footer->header + sizeof(header_t) <= heap->end_address &&
        prev_footer->header->magic == HEAP_MAGIC &&
        prev_footer->header->is_hole) {
        uint32_t prev_size = prev_footer->header->size;
        header_t *prev_header = prev_footer->header;
        prev_header->size += header->size;
        
        // Update our footer to point to the new head
        footer->header = prev_header;
        header = prev_header; // New block starts at prev_header

        // Remove the block from index before re-inserting with new size (since size changed)
        uint32_t iterator = 0;
        while (iterator < heap->index.size && lookup_ordered_array(iterator, &heap->index) != (void *)header)
            iterator++;
        if (iterator < heap->index.size)
            remove_ordered_array(iterator, &heap->index);
        
        // DIAGNOSTIC: check if merge-left created hole spanning PID2
        if ((uintptr_t)header <= 0xFFFFA00000104000ULL && (uintptr_t)header + header->size > 0xFFFFA00000104000ULL) {
            char s[32];
            kprint("[HEAP BUG] merge-left created hole spanning PID2!\n");
            kprint("  hole=0x"); hex64_to_ascii((uintptr_t)header, s); kprint(s);
            kprint(" size=0x"); hex64_to_ascii(header->size, s); kprint(s);
            kprint(" freed_p=0x"); hex64_to_ascii((uintptr_t)p, s); kprint(s); kprint("\n");
            panic("HEAP: merge-left spans PID2 stack");
        }
    }

    insert_ordered_array((void*)header, &heap->index);
}

/* Public free() — acquires heap_lock */
void free(void *p, heap_t *heap) {
    if (p == 0) return;
    uint64_t f = spin_lock_irqsave(&heap_lock);
    free_internal(p, heap);
    spin_unlock_irqrestore(&heap_lock, f);
}


/* --- Ordered Array Helper Implementation --- */

int8_t header_t_less_than(void* a, void* b) {
    return (((header_t*)a)->size < ((header_t*)b)->size) ? 1 : 0;
}

ordered_array_t place_ordered_array(void *addr, uint32_t max_size, lessthan_pred_t less_than) {
    ordered_array_t to_ret;
    to_ret.array = (type_t*)addr;
    memory_set((uint8_t*)addr, 0, max_size*sizeof(type_t));
    to_ret.size = 0;
    to_ret.max_size = max_size;
    to_ret.less_than = less_than;
    return to_ret;
}

void insert_ordered_array(void *item, ordered_array_t *array) {
    if (array->size >= array->max_size) {
        panic("Ordered array: index is full (potential heap overflow)");
    }
    
    uint32_t iterator = 0;
    while (iterator < array->size && array->less_than(array->array[iterator], item))
        iterator++;
    if (iterator == array->size)
        array->array[array->size++] = item;
    else {
        // Shift elements to the right to make room
        for (uint32_t j = array->size; j > iterator; j--) {
            array->array[j] = array->array[j - 1];
        }
        array->array[iterator] = item;
        array->size++;
    }
}

void* lookup_ordered_array(uint32_t i, ordered_array_t *array) {
    return array->array[i];
}

void remove_ordered_array(uint32_t i, ordered_array_t *array) {
    while (i < array->size - 1) {
        array->array[i] = array->array[i+1];
        i++;
    }
    array->size--;
}

extern heap_t *kheap;

void get_heap_stats(heap_stats_t *stats) {
    if (!kheap || !stats) return;
    uint64_t f = spin_lock_irqsave(&heap_lock);
    stats->total_size = (size_t)(kheap->end_address - kheap->start_address);
    stats->max_addr = kheap->max_address;
    
    size_t hole_size = 0;
    for (uint32_t i = 0; i < kheap->index.size; i++) {
        header_t *header = (header_t *)lookup_ordered_array(i, &kheap->index);
        hole_size += header->size;
    }
    stats->free_size = hole_size;
    stats->used_size = stats->total_size - hole_size;
    spin_unlock_irqrestore(&heap_lock, f);
}
