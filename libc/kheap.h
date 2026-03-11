#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/arch_types.h>

#ifndef ARCH_X86_64
#define KHEAP_START         0xC0000000
#define KHEAP_INITIAL_SIZE  0x400000
#define KHEAP_MAX_ADDR      0xD0000000
#else
#define KHEAP_START         0xFFFFA00000000000ULL
#define KHEAP_INITIAL_SIZE  0x1000000 // 16MB initial
#define KHEAP_MAX_ADDR      0xFFFFB00000000000ULL
#endif

#define HEAP_INDEX_SIZE     0x20000
#define HEAP_MAGIC          0x123890AB
#define HEAP_MIN_SIZE       0x70000

/* Size information for a hole/block */
typedef struct {
    uint32_t magic;   // Magic number, used for error checking and identification.
    uint8_t is_hole;  // 1 if this is a hole. 0 if this is a block.
    uint32_t size;    // size of the block, including the end footer.
} header_t;

typedef struct {
    uint32_t magic;     // Magic number, same as in header_t.
    header_t *header; // Pointer to the block header.
} footer_t;

/* Standard ordered array for the hole index */
/* We will inline the ordered_array definition here to avoid extra files */
typedef void* type_t;
typedef int8_t (*lessthan_pred_t)(type_t,type_t);
typedef struct {
    type_t *array;
    uint32_t size;
    uint32_t max_size;
    lessthan_pred_t less_than;
} ordered_array_t;

typedef struct heap {
    ordered_array_t index;
    virt_addr_t start_address; // The start of our allocated space.
    virt_addr_t end_address;   // The end of our allocated space. May be expanded up to max_address.
    virt_addr_t max_address;   // The maximum address the heap can be expanded to.
    uint8_t supervisor;     // Should extra pages requested by us be mapped as supervisor?
    uint8_t readonly;       // Should extra pages requested by us be mapped as read-only?
} heap_t;

/**
 * Create a new heap.
 */
heap_t *create_heap(virt_addr_t start, virt_addr_t end, virt_addr_t max, uint8_t supervisor, uint8_t readonly);

/**
 * Allocates a contiguous region of memory 'size' in size.
 * If page_align==1, it creates that block starting on a page boundary.
 */
void *alloc(size_t size, uint8_t page_align, heap_t *heap);

/**
 * Releases a block allocated with 'alloc'.
 */
// releases a block
void free(void *p, heap_t *heap);
void expand(virt_addr_t new_size, heap_t *heap);
virt_addr_t contract(virt_addr_t new_size, heap_t *heap);

typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    virt_addr_t max_addr;
} heap_stats_t;

void get_heap_stats(heap_stats_t *stats);

void* lookup_ordered_array(uint32_t i, ordered_array_t *array);

/**
 * Allocation wrapper associated with generic kheap
 */
uint64_t kmalloc_int(size_t size, int align, phys_addr_t *phys);
void kfree(void *p);

#endif
