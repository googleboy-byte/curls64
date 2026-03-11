[bits 64]
extern main
global _start

_start:
    ; At entry, stack points to [argc] (64-bit)
    ; [rsp + 8] points to argv[0]
    mov rdi, [rsp]       ; rdi = argc
    lea rsi, [rsp + 8]   ; rsi = argv (array of pointers)
    
    ; Ensure 16-byte stack alignment for C code before call
    and rsp, -16
    
    call main
    
    ; Exit with return value
    mov rdi, rax
    call uabi_exit
    hlt ; Should not reach

global uabi_open
global uabi_read
global uabi_write
global uabi_close
global uabi_readdir
global uabi_getcwd
global uabi_chdir
global uabi_stat
global uabi_lseek
global uabi_mkdir
global uabi_unlink

global uabi_fork
global uabi_exec
global uabi_exit
global uabi_wait
global uabi_getpid
global uabi_pipe
global uabi_dup2
global uabi_dup

global uabi_print
global uabi_getline
global uabi_clear
global uabi_getc
global uabi_gotoxy

global uabi_ps
global uabi_memstat
global uabi_set_debug
global uabi_kill
global uabi_sigaction
global uabi_sleep

; Syscall convention for Curls x64 (int 0x80):
; rax = syscall number
; rdi = arg1 -> moved to rbx
; rsi = arg2 -> moved to rcx
; rdx = arg3 -> stays rdx
; (This is to match the kernel's expectation of rbx/rcx/rdx for simplicity in this phase)

%macro SYSCALL0 1
    mov rax, %1
    int 0x80
    ret
%endmacro

%macro SYSCALL1 1
    push rbx
    mov rax, %1
    mov rbx, rdi
    int 0x80
    pop rbx
    ret
%endmacro

%macro SYSCALL2 1
    push rbx
    push rcx
    mov rax, %1
    mov rbx, rdi
    mov rcx, rsi
    int 0x80
    pop rcx
    pop rbx
    ret
%endmacro

%macro SYSCALL3 1
    push rbx
    push rcx
    mov rax, %1
    mov rbx, rdi
    mov rcx, rsi
    ; rdx already contains arg3
    int 0x80
    pop rcx
    pop rbx
    ret
%endmacro

uabi_open:      SYSCALL2 20
uabi_read:      SYSCALL3 21
uabi_write:     SYSCALL3 22
uabi_close:     SYSCALL1 23
uabi_readdir:   SYSCALL3 24
uabi_getcwd:    SYSCALL2 25
uabi_chdir:     SYSCALL1 26
uabi_stat:      SYSCALL2 27
uabi_lseek:     SYSCALL3 28
uabi_mkdir:     SYSCALL1 52
uabi_unlink:    SYSCALL1 53

uabi_fork:      SYSCALL0 30
uabi_exec:      SYSCALL2 31
uabi_exit:      SYSCALL1 32
uabi_wait:      SYSCALL0 33
uabi_getpid:    SYSCALL0 34
uabi_pipe:      SYSCALL1 35
uabi_dup2:      SYSCALL2 36
uabi_dup:       SYSCALL1 37

uabi_print:     SYSCALL1 40
uabi_getline:   SYSCALL2 41
uabi_clear:     SYSCALL0 42
uabi_getc:      SYSCALL0 43
uabi_gotoxy:    SYSCALL2 44

uabi_ps:        SYSCALL2 45
uabi_memstat:   SYSCALL1 46
uabi_set_debug: SYSCALL1 47
uabi_kill:      SYSCALL2 48
uabi_sigaction: SYSCALL2 49
uabi_sleep:     SYSCALL1 51
