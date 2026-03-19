[bits 32]

global syscall_print
global syscall_exit

section .text

syscall_print:
    push ebx
    mov eax, 40 ; UABI_PRINT
    mov ebx, [esp + 8] ; arg1 (string)
    int 0x80
    pop ebx
    ret

syscall_exit:
    mov eax, 32 ; UABI_EXIT
    int 0x80
    ret ; Should not return
