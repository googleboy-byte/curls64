[global read_eip]
read_eip:
    pop eax
    jmp eax

[global perform_task_switch]
perform_task_switch:
    mov ecx, [esp+4]   ; EIP
    mov edx, [esp+8]   ; Physical address of page directory
    mov ebx, [esp+12]  ; ESP
    mov ebp, [esp+16]  ; EBP
    mov eax, [esp+20]  ; EFLAGS

    mov cr3, edx       ; Switch directory
    mov esp, ebx       ; Switch stack
    
    push eax
    popf               ; Restore eflags
    ; EBP is already set from the load above
    
    mov eax, 0x12345   ; Magic value for read_eip check
    
    jmp ecx            ; Jump to the resumption point

[global jump_to_user_mode]
jump_to_user_mode:
    ; [esp+4] = address to jump to
    ; [esp+8] = stack pointer to use
    cli
    mov ebx, [esp+4]   ; target address
    mov ecx, [esp+8]   ; target stack

    mov ax, 0x23       ; User data segment selector (0x20 | 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push 0x23          ; User Data Segment (SS)
    push ecx           ; User Stack Pointer (ESP)
    pushf              ; Push current eflags
    pop eax
    or eax, 0x200      ; Set the IF (Interrupt Flag) bit to 1
    push eax           ; Push back modified eflags
    push 0x1b          ; User Code Segment (CS = 0x18 | 3)
    push ebx           ; target EIP
    
    iret               ; THE JUMP INTO USER MODE!

; Explicit trampoline that builds a clean ring-3 IRET frame
; on a known-good kernel stack before jumping to user mode.
; void first_user_entry_trampoline(uint32_t entry, uint32_t user_esp);

[global first_user_entry_trampoline]
first_user_entry_trampoline:
    cli
    mov ebx, [esp+4]   ; user entry EIP
    mov ecx, [esp+8]   ; user ESP

    mov ax, 0x10       ; kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Build ring-3 IRET frame on current (kernel) stack
    push 0x23          ; SS (user data)
    push ecx           ; ESP (user stack)
    pushf
    pop eax
    or eax, 0x200      ; IF = 1
    push eax
    push 0x1B          ; CS (user code)
    push ebx           ; EIP

    iret
[global copy_page_physical]
copy_page_physical:
    push ebx
    pushf
    cli

    mov ebx, [esp+12]
    mov ecx, [esp+16]

    mov edx, cr0
    and edx, 0x7FFFFFFF
    mov cr0, edx

    mov edx, 1024
.loop:
    mov eax, [ebx]
    mov [ecx], eax
    add ebx, 4
    add ecx, 4
    dec edx
    jnz .loop

    mov edx, cr0
    or  edx, 0x80000000
    mov cr0, edx

    popf
    pop ebx
    ret
