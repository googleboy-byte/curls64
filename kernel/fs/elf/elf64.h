#ifndef ELF64_H
#define ELF64_H

#include <stdint.h>

typedef uint64_t elf64_addr_t;
typedef uint64_t elf64_off_t;
typedef uint16_t elf64_half_t;
typedef uint32_t elf64_word_t;
typedef int32_t  elf64_sword_t;
typedef uint64_t elf64_xword_t;
typedef int64_t  elf64_sxword_t;

typedef struct {
    uint8_t  e_ident[16];
    elf64_half_t e_type;
    elf64_half_t e_machine;
    elf64_word_t e_version;
    elf64_addr_t e_entry;
    elf64_off_t  e_phoff;
    elf64_off_t  e_shoff;
    elf64_word_t e_flags;
    elf64_half_t e_ehsize;
    elf64_half_t e_phentsize;
    elf64_half_t e_phnum;
    elf64_half_t e_shentsize;
    elf64_half_t e_shnum;
    elf64_half_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    elf64_word_t  p_type;
    elf64_word_t  p_flags;
    elf64_off_t   p_offset;
    elf64_addr_t  p_vaddr;
    elf64_addr_t  p_paddr;
    elf64_xword_t p_filesz;
    elf64_xword_t p_memsz;
    elf64_xword_t p_align;
} elf64_phdr_t;

#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8

#define ELFCLASS32    1
#define ELFCLASS64    2

#define EM_X86_64     62

#endif
