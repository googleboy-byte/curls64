[bits 32]

global syscall_print
global syscall_exit

section .text

syscall_print:
    push ebx
    mov eax, 0 ; SYS_PRINT
    mov ebx, [esp + 8] ; arg1 (string)
    int 0x80
    pop ebx
    ret

syscall_exit:
    mov eax, 1 ; SYS_EXIT
    int 0x80
    ret ; Should not return
