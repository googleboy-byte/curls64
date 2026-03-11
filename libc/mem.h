#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/arch_types.h>

#include <stddef.h>
#include <stdint.h>
void memory_copy(uint8_t *source, uint8_t *dest, size_t nbytes);
void memory_set(uint8_t *dest, uint8_t val, size_t len);
int memory_compare(uint8_t *s1, uint8_t *s2, int n);

/* At this stage there is no 'free' implemented. */
void *kmalloc(size_t size, int align, phys_addr_t *phys_addr);
uint64_t kmalloc_int(size_t size, int align, phys_addr_t *phys_addr);
extern uintptr_t free_mem_addr;

void kfree(void *p); // New free function

// Forward Declaration
typedef struct heap heap_t;
extern heap_t *kheap;

#endif
