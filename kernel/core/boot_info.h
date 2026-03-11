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

extern boot_framebuffer_info_t boot_fb_info;

// Virtual base for kernel mapping of the framebuffer when present.
// Chosen to avoid clashes with KHEAP (0xC0000000..0xD0000000) and PHYSMAP (0xE0000000..).
#define FB_VIRT_BASE 0xF0000000

