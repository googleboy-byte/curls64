[bits 64]

global syscall_print
global syscall_exit

section .text

syscall_print:
    ; rdi = first arg (buffer pointer)
    push rbx
    mov rax, 40 ; UABI_PRINT
    mov rbx, rdi
    int 0x80
    pop rbx
    ret

syscall_exit:
    ; rdi = first arg (exit code)
    push rbx
    mov rax, 32 ; UABI_EXIT
    mov rbx, rdi
    int 0x80
    pop rbx
    ret
