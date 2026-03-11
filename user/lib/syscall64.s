[bits 64]

global syscall_print
global syscall_exit

section .text

syscall_print:
    ; rdi = first arg (buffer pointer)
    push rbx
    mov rax, 0 ; SYS_PRINT
    mov rbx, rdi
    int 0x80
    pop rbx
    ret

syscall_exit:
    ; rdi = first arg (exit code)
    push rbx
    mov rax, 1 ; SYS_EXIT
    mov rbx, rdi
    int 0x80
    pop rbx
    ret
