[extern isr_handler]
[extern irq_handler]
[extern task_switch_rsp]

; Common ISR code
isr_common_stub:
    ; 1. Save CPU state (r15-r8, rbp, rdi, rsi, rdx, rcx, rbx, rax)
    ; Stack Frame Layout (Matches registers_t):
    ; [rsp + 0x00] r15
    ; [rsp + 0x08] r14
    ; [rsp + 0x10] r13
    ; [rsp + 0x18] r12
    ; [rsp + 0x20] r11
    ; [rsp + 0x28] r10
    ; [rsp + 0x30] r9
    ; [rsp + 0x38] r8
    ; [rsp + 0x40] rbp
    ; [rsp + 0x48] rdi
    ; [rsp + 0x50] rsi
    ; [rsp + 0x58] rdx
    ; [rsp + 0x60] rcx
    ; [rsp + 0x68] rbx
    ; [rsp + 0x70] rax
    ; [rsp + 0x78] int_no
    ; [rsp + 0x80] err_code
    ; [rsp + 0x88] rip
    ; [rsp + 0x90] cs
    ; [rsp + 0x98] rflags
    ; [rsp + 0xA0] rsp
    ; [rsp + 0xA8] ss

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; 2. Save segment registers (lower 16-bits are useful)
    mov rax, ds
    push rax
    mov rax, es
    push rax
    push fs
    push gs

    mov rdi, rsp ; registers_t *r
    cld
    call isr_handler

    ; Disable interrupts before checking task_switch_rsp and restoring state.
    ; get_char_noecho uses sti which leaves IF=1 through the C return path.
    cli

    ; Handle task switch if requested (needed for schedule() in UABI_EXIT etc.)
    mov rax, [rel task_switch_rsp]
    test rax, rax
    jz .no_switch
    
    mov qword [rel task_switch_rsp], 0
    mov rsp, rax

.no_switch:
    pop gs
    pop fs
    pop rax
    mov es, ax
    pop rax
    mov ds, ax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16 ; int_no, err_code
    iretq

; Common IRQ code
irq_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Save segments for IRQ too
    mov rax, ds
    push rax
    mov rax, es
    push rax
    push fs
    push gs

    mov rdi, rsp
    cld
    call irq_handler

    mov rax, [rel task_switch_rsp]
    test rax, rax
    jz .irq_no_switch
    
    mov qword [rel task_switch_rsp], 0
    mov rsp, rax

.irq_no_switch:
    pop gs
    pop fs
    pop rax
    mov es, ax
    pop rax
    mov ds, ax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16
    iretq

%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    push qword %1
    jmp isr_common_stub
%endmacro

; Generate all 256 ISR stubs
%assign i 0
%rep 8
    ISR_NOERRCODE i
    %assign i i+1
%endrep

ISR_ERRCODE 8
%assign i 9
ISR_NOERRCODE i
%assign i 10

%rep 5
    ISR_ERRCODE i
    %assign i i+1
%endrep

%assign i 15
%rep 2
    ISR_NOERRCODE i
    %assign i i+1
%endrep

ISR_ERRCODE 17

%assign i 18
%rep 238
    ISR_NOERRCODE i
    %assign i i+1
%endrep

; IRQs are just jumps to isr32..47 essentially, but we use a distinct stub for irqX naming
%macro IRQ_STUB 2
  global irq%1
  irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro

%assign i 0
%assign j 32
%rep 16
    IRQ_STUB i, j
    %assign i i+1
    %assign j j+1
%endrep
