// Boot-time information exposed to the rest of the kernel
#pragma once

#include <stdint.h>

typedef struct boot_framebuffer_info {
    uint8_t  present;   // 0 = no framebuffer provided, 1 = valid info
    uint8_t  type;      // Multiboot2 framebuffer_type
    uint8_t  bpp;       // Bits per pixel
    uint8_t  _pad0;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t addr;      // Physical base address of the framebuffer
} boot_framebuffer_info_t;

typedef struct boot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t _pad;
} boot_mmap_entry_t;

#define MAX_BOOT_MMAP_ENTRIES 32
typedef struct boot_mmap_info {
    uint32_t count;
    boot_mmap_entry_t entries[MAX_BOOT_MMAP_ENTRIES];
} boot_mmap_info_t;

extern boot_framebuffer_info_t boot_fb_info;
extern boot_mmap_info_t boot_mmap_info;

// Virtual base for kernel mapping of the framebuffer when present.
// Chosen to avoid clashes with KHEAP (0xC0000000..0xD0000000) and PHYSMAP (0xE0000000..).
#define FB_VIRT_BASE 0xF0000000

