; Multiboot2 header and 32-to-64 bit trampoline for x86_64 Curls
; Targets 64-bit long mode bring-up

[bits 32]

%define MULTIBOOT2_HEADER_MAGIC        0xE85250D6
%define MULTIBOOT2_BOOTLOADER_MAGIC    0x36D76289
%define MULTIBOOT2_ARCH_I386           0

section .multiboot2
align 8

multiboot2_header_start:
    dd MULTIBOOT2_HEADER_MAGIC         ; magic
    dd MULTIBOOT2_ARCH_I386            ; architecture
    dd multiboot2_header_end - multiboot2_header_start ; header length
    dd -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_ARCH_I386 + (multiboot2_header_end - multiboot2_header_start)) ; checksum

    ; --- Framebuffer request tag (type 5) ---
align 8
mb2_tag_framebuffer:
    dw 5                ; type = 5 (framebuffer)
    dw 0                ; flags = 0 (optional)
    dd mb2_tag_framebuffer_end - mb2_tag_framebuffer ; size
    dd 0                ; width  (0 = any)
    dd 0                ; height (0 = any)
    dd 32               ; depth  (prefer 32 bpp)
mb2_tag_framebuffer_end:

    ; --- End tag (type 0, size 8) ---
align 8
multiboot2_tag_end:
    dw 0                ; type = 0 (end)
    dw 0                ; flags = 0
    dd 8                ; size = 8
multiboot2_header_end:

section .text
align 4

global multiboot2_start
extern kernel_multiboot2_main64

multiboot2_start:
    cli
    
    ; Verify multiboot magic
    cmp eax, MULTIBOOT2_BOOTLOADER_MAGIC
    jne .error

    ; Save Multiboot info pointer (ebx)
    mov edi, ebx

    ; Set up temporary 32-bit stack
    mov esp, initial_stack_top

    ; 1. Build early page tables (PML4 -> PDPT -> PD -> 2MB Page)
    ; Identity map the first 2MB
    
    ; Clear page tables
    mov edi, pml4
    xor eax, eax
    mov ecx, 3 * 4096 / 4 ; PML4, PDPT, PD
    rep stosd
    
    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0b11 ; Present | Writeable
    mov [pml4], eax
    
    ; PDPT[0] -> PD
    mov eax, pd
    or eax, 0b11 ; Present | Writeable
    mov [pdpt], eax
    
    ; PD[0..7] -> 2MB Pages (identity map 0..16MB)
    mov edi, pd
    mov eax, 0b10000011 ; Present | Writeable | Huge
    mov ecx, 8
.map_loop:
    mov [edi], eax
    add edi, 8
    add eax, 0x200000 ; Next 2MB
    loop .map_loop

    ; 2. Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; 3. Enable Long Mode (LME) in EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; 4. Load CR3 with PML4
    mov eax, pml4
    mov cr3, eax

    ; 5. Enable Paging
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; 6. Load 64-bit GDT
    lgdt [gdt64_ptr]

    ; 7. Far jump to 64-bit code segment
    jmp gdt64.code:long_mode_entry

.error:
    ; Hang if bad magic
    hlt
    jmp .error

[bits 64]
long_mode_entry:
    ; Set up 64-bit registers
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up 64-bit stack
    mov rsp, initial_stack_top

    ; Pass Multiboot info pointer (rdi was inherited from edi)
    mov esi, MULTIBOOT2_BOOTLOADER_MAGIC ; magic in rsi (second arg)
    ; rdi already has MBI pointer (from edi)
    
    ; Align stack and call C
    and rsp, -16
    call kernel_multiboot2_main64

.hang:
    cli
    hlt
    jmp .hang

section .data
align 8
gdt64:
    dq 0 ; null
.code equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code segment (long mode, present, ring 0)
.data equ $ - gdt64
    dq (1<<41) | (1<<44) | (1<<47)           ; data segment (present, ring 0)
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64

section .bss
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096

align 16
initial_stack:
    resb 4096
initial_stack_top:
