[bits 64]

[global gdt_flush]
gdt_flush:
    lgdt [rdi]        ; Load GDT from rdi
    
    ; Reload CS
    push 0x08         ; New CS (GDT entry 1)
    lea rax, [rel .flush]
    push rax          ; New RIP
    retfq
.flush:
    ; Reload data segments
    mov ax, 0x10      ; Kernel Data segment (GDT entry 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

[global tss_flush]
tss_flush:
    mov ax, di        ; TSS selector is in di
    ltr ax            ; Load TSS
    ret
