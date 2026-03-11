; Stage 2 Bootloader: Load Kernel and switch to PM
[org 0x8000]
KERNEL_OFFSET equ 0x10000
    mov [BOOT_DRIVE], dl ; Re-confirm boot drive
    
    mov bx, MSG_STAGE2
    call print
    call print_nl

    call load_kernel
    
    mov bx, MSG_SWITCHING_PM
    call print
    call print_nl
    
    call switch_to_pm
    jmp $

%include "boot/print.asm"
%include "boot/disk.asm"
%include "boot/gdt.asm"
%include "boot/print_hex.asm"
%include "boot/32bit_print.asm"
%include "boot/switch_pm.asm"

[bits 16]
load_kernel:
    mov bx, MSG_LOAD_KERNEL
    call print
    call print_nl

    mov ax, KERNEL_OFFSET >> 4
    mov es, ax
    xor bx, bx
    mov dl, [BOOT_DRIVE]
    mov cl, 0x06        ; Kernel starts at sector 6
    mov si, 350         ; Load 350 sectors (~175KB) via SI (16-bit)
    call disk_load_large
    ret

[bits 32]
BEGIN_PM:
    mov ebx, MSG_PROT_MODE
    call print_string_pm
    call KERNEL_OFFSET
    jmp $

BOOT_DRIVE db 0
MSG_STAGE2 db "Curls OS Stage 2 Active", 0
MSG_LOAD_KERNEL db "Loading Kernel (Sector 6)...", 0
MSG_SWITCHING_PM db "Switching to Protected Mode...", 0
MSG_PROT_MODE db "Landed in 32-bit Protected Mode", 0
