#include "boot_info.h"
#include "kernel.h"
#include <stdint.h>

extern void kernel_main(void);

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

typedef struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
    /* We ignore the union with palette/direct color specifics for now. */
} multiboot_tag_framebuffer_t;

boot_framebuffer_info_t boot_fb_info = {
    .present = 0
};

void kernel_multiboot2_main(uint32_t magic, void *mbi_addr) {
    // Default: no framebuffer
    boot_fb_info.present = 0;

    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && mbi_addr) {
        uint8_t *addr = (uint8_t *)mbi_addr;
        uint32_t total_size = *(uint32_t *)addr;
        uint32_t offset = 8; // skip total_size and reserved

        while (offset < total_size) {
            multiboot_tag_t *tag = (multiboot_tag_t *)(addr + offset);
            if (tag->type == 0) {
                break;
            }

            if (tag->type == 8) { // MULTIBOOT_TAG_TYPE_FRAMEBUFFER
                multiboot_tag_framebuffer_t *fb = (multiboot_tag_framebuffer_t *)tag;
                boot_fb_info.present = 1;
                boot_fb_info.addr    = fb->framebuffer_addr;
                boot_fb_info.pitch   = fb->framebuffer_pitch;
                boot_fb_info.width   = fb->framebuffer_width;
                boot_fb_info.height  = fb->framebuffer_height;
                boot_fb_info.bpp     = fb->framebuffer_bpp;
                boot_fb_info.type    = fb->framebuffer_type;
                break;
            }

            // Tags are 8-byte aligned
            offset += (tag->size + 7) & ~7;
        }
    }

    // Continue with normal kernel boot path
    kernel_main();
}

