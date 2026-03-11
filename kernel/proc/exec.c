#include "../core/task.h"
#include "../fs/elf/elf32.h"
#include "../fs/elf/elf64.h"
#include "../cpu/paging.h"
#include "../../libc/mem.h"
#include "../../libc/string.h"
#include "../core/vfs_core.h"
#include "../cpu/isr.h"
#include "../../include/kabi/kabi_v1.h"

#ifdef ARCH_X86_64
#define USER_STACK_TOP  0x00007FFFFFFFF000ULL
#define USER_STACK_SIZE 0x8000 // 32KB for x64
#else
#define USER_STACK_TOP  0xBFFFF000
#define USER_STACK_SIZE 0x4000 // 16KB for x86
#endif

extern int elf_validate(void *image);
extern int elf_load_image_from_buffer(uint8_t *image, size_t size, page_directory_t *pd, elf_load_result_t *out);
int sys_execve(const char *path, char **argv, registers_t *regs);

static virt_addr_t build_user_stack(page_directory_t *pd, char **argv) {
    // 1. Map user stack pages
    for (virt_addr_t v = USER_STACK_TOP - USER_STACK_SIZE; v < USER_STACK_TOP; v += 0x1000) {
        page_t *page = get_page(v, 1, pd);
#ifdef ARCH_X86_64
        if (!PAGE_PRESENT(*page)) {
            uintptr_t frame = pmm_first_free();
            if (frame == (uintptr_t)-1) return 0;
            PAGE_SET_FRAME(page, (uint64_t)frame * 0x1000);
            PAGE_SET_FLAGS(page, MMU_PRESENT | MMU_WRITABLE | MMU_USER);
            frame_add_ref(frame);
        }
#else
        if (!page->present) {
            uintptr_t frame = pmm_first_free();
            if (frame == (uintptr_t)-1) return 0;
            page->frame = frame / 0x1000;
            page->present = 1; page->rw = 1; page->user = 1;
            frame_add_ref(frame);
        }
#endif
    }

    uint32_t argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }

    virt_addr_t sp = USER_STACK_TOP;
    virt_addr_t argv_ptrs[64]; // Limit 64 args
    if (argc > 64) argc = 64;

    // Copy argument strings
    for (int i = (int)argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        sp -= len;
        
        virt_addr_t v_addr = sp;
        size_t bytes_to_copy = len;
        uint8_t *src_ptr = (uint8_t*)argv[i];
        
        while (bytes_to_copy > 0) {
            uintptr_t off = v_addr % 0x1000;
            size_t chunk = 0x1000 - off;
            if (chunk > bytes_to_copy) chunk = bytes_to_copy;
            
            page_t *p = get_page(v_addr, 0, pd);
#ifdef ARCH_X86_64
            uintptr_t phys = PAGE_FRAME(*p) + off;
#else
            uintptr_t phys = PAGE_FRAME(p) + off;
#endif
            memory_copy(src_ptr, (uint8_t*)(PHYSMAP_BASE + phys), chunk);
            
            bytes_to_copy -= chunk;
            src_ptr += chunk;
            v_addr += chunk;
        }
        argv_ptrs[i] = sp;
    }

    // Align stack
    sp &= ~0xF; 

#ifdef ARCH_X86_64
    // 64-bit alignment and conventions
    // Push NULL char*
    sp -= 8;
    {
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(*p) + (sp % 0x1000);
        *(uint64_t*)(PHYSMAP_BASE + phys) = 0;
    }

    // Push argv pointers (64-bit)
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 8;
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(*p) + (sp % 0x1000);
        *(uint64_t*)(PHYSMAP_BASE + phys) = argv_ptrs[i];
    }

    virt_addr_t argv_array_ptr = sp;

    // Push argv
    sp -= 8;
    {
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(*p) + (sp % 0x1000);
        *(uint64_t*)(PHYSMAP_BASE + phys) = argv_array_ptr;
    }

    // Push argc
    sp -= 8;
    {
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(*p) + (sp % 0x1000);
        *(uint64_t*)(PHYSMAP_BASE + phys) = (uint64_t)argc;
    }
#else
    // 32-bit stack layout
    // Push NULL
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(p) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = 0;
    }

    // Push argv pointers
    for (int i = (int)argc - 1; i >= 0; i--) {
        sp -= 4;
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(p) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = argv_ptrs[i];
    }

    virt_addr_t argv_array_ptr = sp;

    // Push argv pointer
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(p) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = argv_array_ptr;
    }

    // Push argc
    sp -= 4;
    {
        page_t *p = get_page(sp, 0, pd);
        uintptr_t phys = PAGE_FRAME(p) + (sp % 0x1000);
        *(uint32_t*)(PHYSMAP_BASE + phys) = argc;
    }
#endif

    // Push fake return address (always 0)
    sp -= sizeof(virt_addr_t);
    {
        page_t *p = get_page(sp, 0, pd);
#ifdef ARCH_X86_64
        uintptr_t phys = PAGE_FRAME(*p) + (sp % 0x1000);
#else
        uintptr_t phys = PAGE_FRAME(p) + (sp % 0x1000);
#endif
        *(virt_addr_t*)(PHYSMAP_BASE + phys) = 0;
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

    int elf_class = elf_validate(image);
    if (elf_class < 0) {
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

    virt_addr_t new_sp = build_user_stack(new_pd, argv);
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

    if (kabi_debug_enabled()) {
        char s[20];
#ifdef ARCH_X86_64
        kprint("[EXEC] Mapping ready. PD=0x"); hex64_to_ascii(new_pd->pml4_phys, s); kprint(s); kprint("\n");
#else
        kprint("[EXEC] Mapping ready. PD=0x"); hex64_to_ascii(new_pd->physicalAddr, s); kprint(s); kprint("\n");
#endif
    }

    // Update registers for IRET
#ifdef ARCH_X86_64
    // 6. Transition to user mode (set registers)
    // In x86_64, argc and argv should be in rdi and rsi
    // according to System V ABI, though they are also on the stack.
    uint64_t argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }

    regs->rip = (uintptr_t)res.entry;
    regs->rsp = (uint64_t)new_sp;
    regs->rax = 0;
    regs->rdi = argc;
    // In our build_user_stack, we return 'sp' which points to 'argc'.
    // System V ABI: argc is at (%rsp), argv is at (%rsp + 8)
    regs->rsi = (uint64_t)(new_sp + 8); 
    regs->rflags = 0x202; // IF | bit 1
    // Selectors for 64-bit User Mode
    regs->cs = 0x1B;
    regs->ss = 0x23;
#else
    regs->eip = res.entry;
    regs->esp = new_sp;
    regs->eax = 0;
    regs->eflags = 0x202; // IF | bit 1
    // Selectors for 32-bit User Mode
    regs->cs = 0x1B;
    regs->ds = 0x23;
    regs->ss = 0x23;
#endif

    if (kabi_debug_enabled()) {
        kprint("[EXEC] About to return to Ring 3\n");
    }
    return 0; 
}
