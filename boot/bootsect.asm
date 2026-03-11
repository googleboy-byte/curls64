; Stage 1 Bootloader: Load Stage 2 from disk
[org 0x7c00]
STAGE2_OFFSET equ 0x8000
 ; or 0x1000

    mov [BOOT_DRIVE], dl ; BIOS sets the boot drive in 'dl'
    mov bp, 0x9000
    mov sp, bp

    mov bx, MSG_STAGE1
    call print
    call print_nl

    call load_stage2
    
    ; Pass the boot drive to stage 2 in dl
    mov dl, [BOOT_DRIVE]
    jmp STAGE2_OFFSET

%include "boot/print.asm"
%include "boot/print_hex.asm"
%include "boot/disk.asm"

[bits 16]
load_stage2:
    mov bx, MSG_LOAD_STAGE2
    call print
    call print_nl

    mov ax, STAGE2_OFFSET >> 4
    mov es, ax
    xor bx, bx
    mov dh, 4            ; Load 4 sectors for Stage 2 (2KB)
    mov dl, [BOOT_DRIVE]
    mov cl, 0x02         ; Stage 2 starts at sector 2
    call disk_load
    ret

BOOT_DRIVE db 0
MSG_STAGE1 db "Curls OS Stage 1", 0
MSG_LOAD_STAGE2 db "Loading Stage 2...", 0

; padding
times 510 - ($-$$) db 0
dw 0xaa55
