#include "elf32.h"
#include "elf64.h"
#include <stdint.h>

int elf_validate(void *image) {
    if (!image) return -1;
    
    uint8_t *ident = (uint8_t*)image;
    if (ident[EI_MAG0] != 0x7F || ident[EI_MAG1] != 'E' || 
        ident[EI_MAG2] != 'L' || ident[EI_MAG3] != 'F') return -1;

    uint8_t class = ident[EI_CLASS];
    if (class == ELFCLASS32) {
        elf32_ehdr_t *h = (elf32_ehdr_t*)image;
        if (h->machine != 3) return -1; // EM_386
        if (h->phnum == 0) return -1;
        return 32;
    } else if (class == ELFCLASS64) {
        elf64_ehdr_t *h = (elf64_ehdr_t*)image;
        if (h->e_machine != EM_X86_64) return -1;
        if (h->e_phnum == 0) return -1;
        return 64;
    }

    return -1;
}

int elf32_validate(void *image) {
    return (elf_validate(image) == 32) ? 0 : -1;
}
