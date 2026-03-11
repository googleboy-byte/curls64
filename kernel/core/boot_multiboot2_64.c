#include "boot_info.h"
#include "kernel.h"
#include <stdint.h>

/**
 * @file boot_multiboot2_64.c
 * @brief 64-bit entry point for Multiboot2 boot.
 * 
 * This file replaces boot_multiboot2.c in 64-bit builds.
 * It parses Multiboot2 tags using 64-bit safe pointers.
 */

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
} multiboot_tag_framebuffer_t;

boot_framebuffer_info_t boot_fb_info = {
    .present = 0
};

// Minimal serial print for verification
#define COM1 0x3f8
static void serial_putc(char c) {
    while ((*(volatile uint8_t*)(uintptr_t)(COM1 + 5) & 0x20) == 0);
    asm volatile("outb %b0, %w1" : : "a"(c), "Nd"(COM1));
}

static void serial_print(const char *s) {
    while (*s) serial_putc(*s++);
}

/**
 * kernel_multiboot2_main64
 * Entry point called by multiboot2_entry64.asm after long mode is enabled.
 * rdi = mbi_addr, rsi = magic
 */
void kernel_multiboot2_main64(void *mbi_addr, uint64_t magic) {
    serial_print("\r\n--- Curls x86_64 First Light ---\r\n");
    serial_print("Long mode enabled, entering 64-bit C code...\r\n");

    // Default: no framebuffer
    boot_fb_info.present = 0;

    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && mbi_addr) {
        uint8_t *addr = (uint8_t *)mbi_addr;
        uint32_t total_size = *(uint32_t *)addr;
        uint32_t offset = 8; // skip total_size and reserved

        while (offset < total_size) {
            multiboot_tag_t *tag = (multiboot_tag_t *)(addr + offset);
            
            // End tag
            if (tag->type == 0) {
                break;
            }

            // Framebuffer tag
            if (tag->type == 8) {
                multiboot_tag_framebuffer_t *fb = (multiboot_tag_framebuffer_t *)tag;
                boot_fb_info.present = 1;
                boot_fb_info.addr    = fb->framebuffer_addr;
                boot_fb_info.pitch   = fb->framebuffer_pitch;
                boot_fb_info.width   = fb->framebuffer_width;
                boot_fb_info.height  = fb->framebuffer_height;
                boot_fb_info.bpp     = fb->framebuffer_bpp;
                boot_fb_info.type    = fb->framebuffer_type;
            }

            // Tags are 8-byte aligned
            offset += (tag->size + 7) & ~7;
            
            // Safety break for corrupted headers
            if (offset == 0 || tag->size == 0) break;
        }
    }

    // Call architecture-independent kernel entry
    // kernel_main();
    serial_print("Verification complete. Hanging.\r\n");
    while(1);
}
