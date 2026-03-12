; 64-bit process helpers for x86_64 kernel
; Provides first_user_entry_trampoline for transitioning to user mode

[bits 64]

; void first_user_entry_trampoline(uint64_t entry, uint64_t user_rsp);
; System V ABI: entry in RDI, user_rsp in RSI
; Builds a ring-3 IRET frame on the current kernel stack and iretq's into userland.
[global first_user_entry_trampoline]
first_user_entry_trampoline:
    cli

    ; Load user data segment selectors (GDT_USER_DS | RPL=3 = 0x20 | 3 = 0x23)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build IRET frame on kernel stack:
    ;   SS       = 0x23 (user data)
    ;   RSP      = user_rsp (RSI)
    ;   RFLAGS   = IF=1 (0x202)
    ;   CS       = 0x1B (user code = GDT_USER_CS | RPL=3 = 0x18 | 3)
    ;   RIP      = entry (RDI)
    push 0x23           ; SS
    push rsi            ; RSP (user stack)
    push 0x202          ; RFLAGS (IF=1)
    push 0x1B           ; CS
    push rdi            ; RIP (entry point)

    iretq
