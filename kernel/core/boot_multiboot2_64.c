#include "boot_info.h"
#include "kernel.h"
#include <stdint.h>
#include "../../libc/mem.h"
#include "../../libc/string.h"
#include "../../kernel/cpu/paging.h"
#include "../arch/x86_64/mmu/mmu.h"

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

typedef struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} multiboot_mmap_entry_t;

typedef struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot_mmap_entry_t entries[0];
} multiboot_tag_mmap_t;

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

boot_mmap_info_t boot_mmap_info = {
    .count = 0
};

// --- Minimal Serial/Kprint for Verification ---
#define COM1 0x3f8

// outb/inb/serial_init/write_serial kept for bootloader initialization
static void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init() {
    outb(COM1 + 1, 0x00);    // Disable all interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x01);    // Set divisor to 1 (lo byte) 115200 baud
    outb(COM1 + 1, 0x00);    //                  (hi byte)
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int is_transmit_empty() {
    return inb(COM1 + 5) & 0x20;
}

void write_serial(char a) {
    while (is_transmit_empty() == 0);
    outb(COM1, a);
}

void kprint(const char *s) {
    while (*s) {
        if (*s == '\n') write_serial('\r');
        write_serial(*s++);
    }
}

void kernel_multiboot2_main64(void *mbi_addr, uint64_t magic) {
    serial_init();
    kprint("\n\n*** CURLS X86_64 BOOTLOADER HANDOFF ***\n");
    
    boot_fb_info.present = 0;
    boot_mmap_info.count = 0;

    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC && mbi_addr) {
        uint8_t *addr = (uint8_t *)mbi_addr;
        uint32_t total_size = *(uint32_t *)addr;
        uint32_t offset = 8;

        while (offset < total_size) {
            multiboot_tag_t *tag = (multiboot_tag_t *)(addr + offset);
            if (tag->type == 0) break;

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
            
            if (tag->type == 6) {
                multiboot_tag_mmap_t *mmap = (multiboot_tag_mmap_t *)tag;
                uint32_t entries = (mmap->size - 16) / mmap->entry_size;
                for (uint32_t i = 0; i < entries && i < MAX_BOOT_MMAP_ENTRIES; i++) {
                    boot_mmap_info.entries[i].addr = mmap->entries[i].addr;
                    boot_mmap_info.entries[i].len  = mmap->entries[i].len;
                    boot_mmap_info.entries[i].type = mmap->entries[i].type;
                    boot_mmap_info.count++;
                }
            }
            offset += (tag->size + 7) & ~7;
            if (offset == 0 || tag->size == 0) break;
        }
    }
    
    // Call architecture-independent kernel entry
    kprint("Calling kernel_main()...\n");
    kernel_main();
    while(1);
}
