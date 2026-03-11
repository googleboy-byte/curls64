#include "elf32.h"
#include "../../cpu/paging.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "../../modules/drivers/screen.h"
#include "../../../include/module/module_abi_v1.h"

int elf32_load_segments(void *image, page_directory_t *pd);

int elf_load_image_from_buffer(uint8_t *image, size_t size, page_directory_t *pd, elf_load_result_t *out) {
    if (!image || !pd || !out) return -1;
    
    elf32_ehdr_t *eh = (elf32_ehdr_t*)image;
    out->entry = eh->entry;
    out->stack_top = 0xBFFFF000; // Standard for our kernel

    return elf32_load_segments(image, pd);
}

int elf32_load_segments(void *image, page_directory_t *pd) {
    if (!image || !pd) return -1;
    
    elf32_ehdr_t *eh = (elf32_ehdr_t*)image;
    elf32_phdr_t *ph = (elf32_phdr_t*)((uintptr_t)image + eh->phoff);

    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type != PT_LOAD) continue;

        uint32_t va = ph[i].vaddr;
        uint32_t memsz = ph[i].memsz;
        uint32_t filesz = ph[i].filesz;
        uint32_t offset = ph[i].offset;

        char s[16];
        if (kabi_debug_enabled()) {
            kprint("[ELF] PT_LOAD: vaddr=0x"); hex_to_ascii(va, s); kprint(s);
            kprint(" memsz=0x"); hex_to_ascii(memsz, s); kprint(s);
            kprint(" filesz=0x"); hex_to_ascii(filesz, s); kprint(s);
            kprint(" offset=0x"); hex_to_ascii(offset, s); kprint(s);
            kprint("\n");
        }

        // Map user range
        for (uint32_t v = va & 0xFFFFF000; v < va + memsz; v += 0x1000) {
            page_t *page = get_page(v, 1, pd);
            if (!page->frame) {
                uint32_t frame = pmm_first_free();
                if (frame == (uint32_t)-1) return -1;
                
                page->frame = frame;
                page->present = 1;
                page->rw = 1;
                page->user = 1;
                frame_add_ref(frame);
            }
        }

        // Copy segment data using PHYSMAP
        uint32_t bytes_left = filesz;
        uint32_t src_ptr = (uint32_t)image + offset;
        uint32_t dest_va = va;
        
        while (bytes_left > 0) {
            uint32_t va_offset = dest_va % 0x1000;
            uint32_t to_copy = 0x1000 - va_offset;
            if (to_copy > bytes_left) to_copy = bytes_left;
            
            page_t *page = get_page(dest_va, 0, pd);
            if (!page) return -1;
            
            uint32_t phys = (page->frame * 0x1000) + va_offset;
            memory_copy((uint8_t*)src_ptr, (uint8_t*)(PHYSMAP_BASE + phys), to_copy);
            
            bytes_left -= to_copy;
            src_ptr += to_copy;
            dest_va += to_copy;
        }

        // Zero BSS tail
        if (memsz > filesz) {
            if (kabi_debug_enabled()) {
                kprint("[ELF] Zeroing BSS tail: 0x"); hex_to_ascii(memsz - filesz, s); kprint(s); kprint(" bytes\n");
            }
            uint32_t bss_left = memsz - filesz;
            while (bss_left > 0) {
                uint32_t va_offset = dest_va % 0x1000;
                uint32_t to_zero = 0x1000 - va_offset;
                if (to_zero > bss_left) to_zero = bss_left;
                
                page_t *page = get_page(dest_va, 0, pd);
                if (!page) return -1;
                
                uint32_t phys = (page->frame * 0x1000) + va_offset;
                memory_set((uint8_t*)(PHYSMAP_BASE + phys), 0, to_zero);
                
                bss_left -= to_zero;
                dest_va += to_zero;
            }
        }
    }

    return 0;
}

/* --- Module Registration --- */
kabi_module_t __kabi_module_elf = {
    .name           = "elf_loader",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = NULL, // Stateless loader, no init required
    .exit           = NULL,
    .description    = "ELF32 binary loader"
};
