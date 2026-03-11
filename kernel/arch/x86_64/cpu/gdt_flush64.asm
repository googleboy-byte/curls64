[global gdt_flush]
gdt_flush:
    lgdt [rdi]        ; Load GDT from rdi
    mov ax, 0x10      ; Kernel Data segment (GDT entry 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Perform far return to reload CS
    push 0x08         ; New CS (GDT entry 1)
    lea rax, [rel .flush]
    push rax          ; New RIP
    retfq
.flush:
    ret

[global tss_flush]
tss_flush:
    mov ax, di        ; TSS selector is in di
    ltr ax            ; Load TSS
    ret
