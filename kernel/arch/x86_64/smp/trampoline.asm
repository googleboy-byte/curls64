[BITS 16]
org 0x0000

trampoline_start:
    ; Breadcrumb: write 0xCAFE to physical 0x6000
    xor ax, ax
    mov ds, ax
    mov word [0x6000], 0xCAFE

    cli
    cld

    jmp short setup_start

align 4
pml4_addr:    dd 0
gdt64_ptr:    dw 0
              dq 0
ap_stack_ptr: dq 0
ap_entry_ptr: dq 0
cpu_id:       dd 0

align 4
gdt32_ptr:    dw gdt32_end - gdt32 - 1
              dd 0x70000 + gdt32
align 8
gdt32:
    dq 0                  ; null
    dq 0x00CF9A000000FFFF ; 32-bit code
    dq 0x00CF92000000FFFF ; 32-bit data
gdt32_end:

setup_start:
    ; Load a minimal GDT for protected mode transition
    lgdt [cs:gdt32_ptr]

    ; Enable protected mode
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp dword 0x08:(0x70000 + trampoline_pm32)

[BITS 32]
trampoline_pm32:
    ; Set up segment registers for 32-bit protected mode
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load the 64-bit GDT (the kernel's real GDT)
    lgdt [0x70000 + gdt64_ptr]

    ; Enable PAE and long mode
    mov eax, cr4
    or  eax, (1 << 5)   ; PAE
    mov cr4, eax

    ; Load PML4 — BSP writes this value before SIPI
    mov eax, [0x70000 + pml4_addr]
    mov cr3, eax

    ; Enable long mode in EFER
    mov ecx, 0xC0000080  ; IA32_EFER
    rdmsr
    or  eax, (1 << 8)    ; LME
    wrmsr

    ; Enable paging + protected mode
    mov eax, cr0
    or  eax, (1 << 31) | 1
    mov cr0, eax

    jmp 0x08:(0x70000 + trampoline_lm64)

[BITS 64]
trampoline_lm64:
    ; Load 64-bit data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; Load per-AP stack
    mov rsp, [0x70000 + ap_stack_ptr]

    ; Load cpu_id to rdi (1st arg)
    mov edi, [0x70000 + cpu_id]

    ; Print 'A' to COM1 (0x3F8)
    mov dx, 0x3F8
    mov al, 'A'
    out dx, al

    ; Call ap_entry(cpu_id) in C
    ; RDI already has cpu_id from line 90 (edi)
    mov rax, [0x70000 + ap_entry_ptr]
    call rax

    cli
.hlt:
    hlt
    jmp .hlt

trampoline_end:
