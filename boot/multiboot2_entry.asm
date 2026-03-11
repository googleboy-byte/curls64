; Multiboot2 header and entry point for GRUB
; Targets 32-bit x86 and hands control to kernel_multiboot2_main(magic, mbi)

[bits 32]

%define MULTIBOOT2_HEADER_MAGIC        0xE85250D6
%define MULTIBOOT2_BOOTLOADER_MAGIC    0x36D76289
%define MULTIBOOT2_ARCH_I386           0

section .multiboot2
align 8

multiboot2_header_start:
    dd MULTIBOOT2_HEADER_MAGIC         ; magic
    dd MULTIBOOT2_ARCH_I386            ; architecture
    dd multiboot2_header_end - multiboot2_header_start ; header length
    dd -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH_I386 + (multiboot2_header_end - multiboot2_header_start)) ; checksum

    ; --- Framebuffer request tag (type 5) ---
    ; Request "any" resolution, prefer 32bpp linear framebuffer.
align 8
mb2_tag_framebuffer:
    dw 5                ; type = 5 (framebuffer)
    dw 0                ; flags = 0 (optional)
    dd mb2_tag_framebuffer_end - mb2_tag_framebuffer ; size
    dd 0                ; width  (0 = any)
    dd 0                ; height (0 = any)
    dd 32               ; depth  (prefer 32 bpp)
mb2_tag_framebuffer_end:

    ; --- End tag (type 0, size 8) ---
align 8
multiboot2_tag_end:
    dw 0                ; type = 0 (end)
    dw 0                ; flags = 0
    dd 8                ; size = 8

multiboot2_header_end:


section .text
align 4

global multiboot2_start
extern kernel_multiboot2_main

; GRUB enters here with:
;   EAX = MULTIBOOT2_BOOTLOADER_MAGIC
;   EBX = pointer to multiboot2 info structure
multiboot2_start:
    cli

    ; Set up a simple 4 KiB stack in .bss for early C code
    mov esp, mb2_boot_stack_top

    ; Pass (magic, mbi) to C
    push ebx
    push eax
    call kernel_multiboot2_main

.hang:
    cli
    hlt
    jmp .hang


section .bss
align 16

global mb2_boot_stack
global mb2_boot_stack_top

mb2_boot_stack:
    resb 4096
mb2_boot_stack_top:

