#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>
#include "../../../include/module/module_abi_v1.h"

typedef struct __attribute__((packed)) {
    uint32_t nfiles; // The number of files in the ramdisk.
} initrd_header_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;    // Magic number, for error checking.
    char name[64];    // Filename.
    uint32_t offset;  // Offset in the initrd that the file starts.
    uint32_t length;  // Length of the file.
} initrd_file_header_t;

void fs_initrd_init();

#endif
