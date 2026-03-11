#include "../core/task.h"
#include "../fs/elf/elf32.h"
#include "../cpu/paging.h"
#include "../../libc/mem.h"
#include "../../libc/string.h"
#include "../core/vfs_core.h"
#include "../cpu/isr.h"
#include "../../include/kabi/kabi_v1.h"

#define USER_STACK_TOP 0xBFFFF000
#define USER_STACK_SIZE 0x4000 // 16KB

extern int elf32_validate(void *image);
extern int elf32_load_segments(void *image, page_directory_t *pd);
extern int elf_load_image_from_buffer(uint8_t *image, size_t size, page_directory_t *pd, elf_load_result_t *out);
int sys_execve(const char *path, char **argv, registers_t *regs);

static uint32_t build_user_stack(page_directory_t *pd, char **argv) {
    // 1. Map user stack page (8KB for simplicity)
    for (uint32_t v = USER_STACK_TOP - 0x2000; v < USER_STACK_TOP; v += 0x1000) {
        page_t *page = get_page(v, 1, pd);
        if (!page->frame) {
            uint32_t frame = pmm_first_free();
            if (frame == (uint32_t)-1) return 0;
            page->frame = frame;
            page->present = 1; page->rw = 1; page->user = 1;
            frame_add_ref(frame);
        }
    }

    uint32_t argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }

    // Temporarily switch to the new page directory to copy strings
    // Or use PHYSMAP. Using PHYSMAP is safer.
    uint32_t sp = USER_STACK_TOP;
    uint32_t argv_ptrs[64]; // Limit 64 args
    if (argc > 64) argc = 64;

    for (int i = (int)argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        
        // Copy string via PHYSMAP
        uint32_t v_addr = sp;
        uint32_t bytes_to_copy = len;
        uint32_t src_ptr = (uint32_t)argv[i];
        
        while (bytes_to_copy > 0) {
            uint32_t off = v_addr % 0x1000;
            uint32_t chunk = 0x1000 - off;
            if (chunk > bytes_to_copy) chunk = bytes_to_copy;
            
            page_t *p = get_page(v_addr, 0, pd);
            uint32_t phys = (p->frame * 0x1000) + off;
            memory_copy((uint8_t*)src_ptr, (uint8_t*)(PHYSMAP_BASE + phys), chunk);
            
            bytes_to_copy -= chunk;
            src_ptr += chunk;
            v_addr += chunk;
        }
        argv_ptrs[i] = sp;
    }

    sp &= ~0xF; // 16-byte align
    
    // Push NULL pointer
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uint32_t phys = (p->frame * 0x1000) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = 0;
    }

    // Push argv pointers
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 4;
        page_t *p = get_page(sp, 0, pd);
        uint32_t phys = (p->frame * 0x1000) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = argv_ptrs[i];
    }

    uint32_t argv_array_ptr = sp; // Pointer to argv[0]

    // Push argv (the pointer to the array)
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uint32_t phys = (p->frame * 0x1000) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = argv_array_ptr;
    }

    // Push argc
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uint32_t phys = (p->frame * 0x1000) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = argc;
    }

    // Push fake return address
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uint32_t phys = (p->frame * 0x1000) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = 0;
    }

    return sp;
}

int sys_execve(const char *path, char **argv, registers_t *regs) {
    if (kabi_debug_enabled()) {
        kprint("[EXEC] Path: "); kprint((char*)path); kprint("\n");
    }
    fs_node_t *node = vfs_resolve_path(path);
    if (!node) return -KABI_ENOENT;

    uint8_t *image = (uint8_t*)kmalloc(node->length, 0, 0);
    if (!image) return -KABI_ENOMEM;
    
    if (read_fs(node, 0, node->length, image) != node->length) {
        kfree(image);
        return -KABI_EIO;
    }

    if (elf32_validate(image) != 0) {
        kfree(image);
        return -KABI_EINVAL;
    }

    page_directory_t *new_pd = clone_page_directory(kernel_directory);
    elf_load_result_t res;
    if (elf_load_image_from_buffer(image, node->length, new_pd, &res) != 0) {
        free_page_directory(new_pd);
        kfree(image);
        if (node->flags & FS_TRANSIENT) kfree(node);
        return -KABI_EIO;
    }
    kfree(image);
    if (node->flags & FS_TRANSIENT) kfree(node);

    uint32_t new_sp = build_user_stack(new_pd, argv);
    if (!new_sp) {
        free_page_directory(new_pd);
        return -KABI_ENOMEM;
    }

    // Replace current task's PD
    page_directory_t *old_pd = (page_directory_t*)current_task->page_directory;
    current_task->page_directory = new_pd;
    switch_page_directory(new_pd); // Activate immediately
    
    if (old_pd != kernel_directory) {
        free_page_directory(old_pd);
    }

    char s[16];
    if (kabi_debug_enabled()) {
        kprint("[EXEC] Setting EIP=0x"); hex_to_ascii(res.entry, s); kprint(s);
        kprint(" ESP=0x"); hex_to_ascii(new_sp, s); kprint(s);
        kprint(" PD=0x"); hex_to_ascii(new_pd->physicalAddr, s); kprint(s);
        kprint("\n");
    }

    // Update registers for IRET
    regs->eip = res.entry;
    regs->esp = new_sp;
    regs->eax = 0; // Return 0 to new program (though it starts at entry)

    // Ensure segments are correct
    regs->cs = 0x1B;
    regs->ds = 0x23;
    regs->ss = 0x23;

    if (kabi_debug_enabled()) {
        kprint("[EXEC] About to return from sys_execve\n");
    }
    return 0; 
}
