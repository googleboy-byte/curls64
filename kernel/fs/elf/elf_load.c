#include "elf32.h"
#include "elf64.h"
#include "../../cpu/paging.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "../../modules/drivers/screen.h"
#include "../../../include/module/module_abi_v1.h"

int elf32_load_segments(void *image, page_directory_t *pd);
int elf64_load_segments(void *image, page_directory_t *pd);

int elf_load_image_from_buffer(uint8_t *image, size_t size, page_directory_t *pd, elf_load_result_t *out) {
    if (!image || !pd || !out) return -1;
    
    uint8_t class = image[EI_CLASS];
    if (class == ELFCLASS32) {
        elf32_ehdr_t *eh = (elf32_ehdr_t*)image;
        out->entry = eh->entry;
        out->stack_top = 0xBFFFF000;
        return elf32_load_segments(image, pd);
    } else if (class == ELFCLASS64) {
        elf64_ehdr_t *eh = (elf64_ehdr_t*)image;
        out->entry = eh->e_entry;
        // 64-bit canonical user stack top
        out->stack_top = 0x00007FFFFFFFF000;
        return elf64_load_segments(image, pd);
    }
    
    return -1;
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

        // Map user range
        for (uint64_t v = va & ~0xFFFULL; v < (uint64_t)va + memsz; v += 0x1000) {
            page_t *page = get_page(v, 1, pd);
#ifdef ARCH_X86_64
            if (!PAGE_PRESENT(*page)) {
                uintptr_t frame = pmm_first_free();
                if (frame == (uintptr_t)-1) return -1;
                PAGE_SET_FRAME(page, (uint64_t)frame * 0x1000);
                PAGE_SET_FLAGS(page, MMU_PRESENT | MMU_WRITABLE | MMU_USER);
                frame_add_ref(frame);
            } else {
                // If it was already present (identity map), ensure it has WRITABLE and USER bits
                PAGE_SET_FLAGS(page, MMU_PRESENT | MMU_WRITABLE | MMU_USER);
            }
#else
            if (!page->present) {
                uint32_t frame = pmm_first_free();
                if (frame == (uint32_t)-1) return -1;
                page->frame = frame / 0x1000;
                page->present = 1; page->rw = 1; page->user = 1;
                frame_add_ref(frame);
            }
#endif
        }

        // Copy segment data using PHYSMAP
        uint64_t bytes_left = filesz;
        uintptr_t src_ptr = (uintptr_t)image + offset;
        uint64_t dest_va = va;
        
        while (bytes_left > 0) {
            uint64_t va_offset = dest_va % 0x1000;
            uint64_t to_copy = 0x1000 - va_offset;
            if (to_copy > bytes_left) to_copy = bytes_left;
            
            page_t *page = get_page(dest_va, 0, pd);
            if (!page) return -1;
            
#ifdef ARCH_X86_64
            uintptr_t phys = PAGE_FRAME(*page) + va_offset;
#else
            uintptr_t phys = PAGE_FRAME(page) + va_offset;
#endif
            memory_copy((uint8_t*)src_ptr, (uint8_t*)(PHYSMAP_BASE + phys), (uint32_t)to_copy);
            
            bytes_left -= to_copy;
            src_ptr += to_copy;
            dest_va += to_copy;
        }

        // Zero BSS tail
        if (memsz > filesz) {
            uint64_t bss_left = memsz - filesz;
            while (bss_left > 0) {
                uint64_t va_offset = dest_va % 0x1000;
                uint64_t to_zero = 0x1000 - va_offset;
                if (to_zero > bss_left) to_zero = bss_left;
                
                page_t *page = get_page(dest_va, 0, pd);
                if (!page) return -1;
                
#ifdef ARCH_X86_64
                uintptr_t phys = PAGE_FRAME(*page) + va_offset;
#else
                uintptr_t phys = PAGE_FRAME(page) + va_offset;
#endif
                memory_set((uint8_t*)(PHYSMAP_BASE + phys), 0, (uint32_t)to_zero);
                
                bss_left -= to_zero;
                dest_va += to_zero;
            }
        }
    }

    return 0;
}

int elf64_load_segments(void *image, page_directory_t *pd) {
#ifdef ARCH_X86_64
    if (!image || !pd) return -1;
    
    elf64_ehdr_t *eh = (elf64_ehdr_t*)image;
    elf64_phdr_t *ph = (elf64_phdr_t*)((uintptr_t)image + eh->e_phoff);

    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;

        uint64_t va = ph[i].p_vaddr;
        uint64_t memsz = ph[i].p_memsz;
        uint64_t filesz = ph[i].p_filesz;
        uint64_t offset = ph[i].p_offset;

        // Map user range
        // FIX: Always allocate fresh frames and zero them to avoid reusing 
        // kernel identity-mapped frames that contain garbage.
        for (uint64_t v = va & ~0xFFFULL; v < va + memsz; v += 0x1000) {
            uint32_t frame_idx = pmm_first_free();
            if (frame_idx == (uint32_t)-1) return -1;
            
            pmm_set_frame(frame_idx);
            phys_addr_t phys = (phys_addr_t)frame_idx * 0x1000;
            
            // Zero the entire frame before use
            memory_set((uint8_t*)(PHYSMAP_BASE + phys), 0, 0x1000);
            
            // Map the frame into the user page directory
            if (mmu_map_page(pd, v, phys, MMU_PRESENT | MMU_WRITABLE | MMU_USER) != 0) {
                return -1;
            }
            frame_add_ref(frame_idx);
        }

        // Copy segment data using PHYSMAP
        uint64_t bytes_left = filesz;
        uintptr_t src_ptr = (uintptr_t)image + (uintptr_t)offset;
        uint64_t dest_va = va;
        
        while (bytes_left > 0) {
            uint64_t va_offset = dest_va % 0x1000;
            uint64_t to_copy = 0x1000 - va_offset;
            if (to_copy > bytes_left) to_copy = bytes_left;
            
            page_t *page = get_page(dest_va, 0, pd);
            if (!page) return -1;
            
            uintptr_t phys = PAGE_FRAME(*page) + (uintptr_t)va_offset;
            memory_copy((uint8_t*)src_ptr, (uint8_t*)(PHYSMAP_BASE + phys), (uint32_t)to_copy);
            
            bytes_left -= to_copy;
            src_ptr += to_copy;
            dest_va += to_copy;
        }

        // BSS tail zeroing is now implicitly handled by the initial frame zeroing,
        // but we preserve the explicit logic for clarity and completeness.
        if (memsz > filesz) {
            uint64_t bss_left = memsz - filesz;
            while (bss_left > 0) {
                uint64_t va_offset = dest_va % 0x1000;
                uint64_t to_zero = 0x1000 - va_offset;
                if (to_zero > bss_left) to_zero = bss_left;
                
                page_t *page = get_page(dest_va, 0, pd);
                if (!page) return -1;
                
                uintptr_t phys = PAGE_FRAME(*page) + (uintptr_t)va_offset;
                memory_set((uint8_t*)(PHYSMAP_BASE + phys), 0, (uint32_t)to_zero);
                
                bss_left -= to_zero;
                dest_va += to_zero;
            }
        }
    }
    return 0;
#else
    return -1;
#endif
}

/* --- Module Registration --- */
kabi_module_t __kabi_module_elf = {
    .name           = "elf_loader",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0110, // Incremented version for ELF64 support
    .init           = NULL,
    .exit           = NULL,
    .description    = "ELF32/64 binary loader"
};
