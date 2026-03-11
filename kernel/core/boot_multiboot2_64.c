#include "boot_info.h"
#include "kernel.h"
#include <stdint.h>
#include "../../libc/mem.h"
#include "../../libc/string.h"
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
    outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
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

void panic(char *message) {
    kprint("\n!!! KERNEL PANIC !!!\n");
    kprint(message);
    kprint("\nSystem Halted.\n");
    while (1) {
        asm volatile("hlt");
    }
}

void kernel_multiboot2_main64(void *mbi_addr, uint64_t magic) {
    serial_init();
    kprint("--- Phase 3: Paging64 Verification ---\n");
    // Default: no info
    boot_fb_info.present = 0;
    boot_mmap_info.count = 0;

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
            
            // Memory map tag
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

            // Tags are 8-byte aligned
            offset += (tag->size + 7) & ~7;
            
            // Safety break for corrupted headers
            if (offset == 0 || tag->size == 0) break;
        }
    }
    // --- Phase 3 Verification ---
    // Minimal serial setup (re-using COM1 from previous phase if needed)
    #define COM1 0x3f8
    void serial_print_str(const char *s); // forward decl
    
    // Create a temporary MMU context for verification
    mmu_context_t verify_ctx;
    phys_addr_t pml4_phys;
    
    kprint("Allocating PML4...\n");
    mmu_table_t *pml4_virt = (mmu_table_t*)kmalloc(sizeof(mmu_table_t), 1, &pml4_phys);
    kprint("PML4 Phys: ");
    char hex_pml4[20]; hex64_to_ascii(pml4_phys, hex_pml4); kprint(hex_pml4); kprint("\n");
    
    memory_set((uint8_t*)pml4_virt, 0, sizeof(mmu_table_t));
    verify_ctx.pml4_phys = pml4_phys;
    verify_ctx.pml4_virt = pml4_virt;

    kprint("Mapping High Canonical address...\n");
    virt_addr_t test_virt = 0xFFFF800000000000ULL;
    phys_addr_t test_phys = 0x100000;
    
    mmu_map_page(&verify_ctx, test_virt, test_phys, MMU_WRITABLE);
    
    kprint("Identity mapping low 16MB...\n");
    for (uint64_t low = 0; low < 0x1000000; low += 0x1000) {
        mmu_map_page(&verify_ctx, low, low, MMU_WRITABLE);
    }
    
    kprint("Switching CR3 to 0x");
    hex_to_ascii(verify_ctx.pml4_phys, hex_pml4); kprint(hex_pml4); kprint("...\n");
    mmu_switch(&verify_ctx);
    kprint("Switch successful!\n");
    
    kprint("Attempting access at ");
    char hex_virt[20]; hex64_to_ascii(test_virt, hex_virt); kprint(hex_virt); kprint("...\n");
    uint32_t *p = (uint32_t*)test_virt;
    uint32_t val = *p; // Should read data from physical 1MB
    
    // If we reach here, mapping worked!
    kprint("MMU High Canonical Mapping Success! Value: 0x");
    char hex[16];
    hex64_to_ascii(val, hex);
    kprint(hex);
    kprint("\n");

    // Call architecture-independent kernel entry
    // kernel_main();
    while(1);
}
