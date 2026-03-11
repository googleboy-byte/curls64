#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <stddef.h>

void memory_copy(uint8_t *source, uint8_t *dest, int nbytes);
void memory_set(uint8_t *dest, uint8_t val, uint32_t len);
int memory_compare(uint8_t *s1, uint8_t *s2, int n);

/* At this stage there is no 'free' implemented. */
void *kmalloc(size_t size, int align, uint32_t *phys_addr);
uint32_t kmalloc_int(size_t size, int align, uint32_t *phys_addr);
void kfree(void *p); // New free function

// Forward Declaration
typedef struct heap heap_t;
extern heap_t *kheap;

#endif
