#include "elf32.h"
#include <stdint.h>

int elf32_validate(void *image) {
    if (!image) return -1;
    
    elf32_ehdr_t *h = (elf32_ehdr_t*)image;

    if (h->magic != ELF_MAGIC) return -1;
    
    // Check machine type (EM_386 is 3)
    if (h->machine != 3) return -1;
    
    // Should have program headers
    if (h->phnum == 0) return -1;

    return 0;
}
