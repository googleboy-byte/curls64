; disk_load: ES:BX = target, DH = sectors, DL = drive, CL = start_sector
; Handles track/head boundaries for 1.44MB Floppies (SPT=18, Heads=2)
disk_load:
    pusha
    push es
    
    push dx      ; Save original dx (sectors/drive)
    
    mov ax, dx
    shr ax, 8    ; al = dh (sectors)
    xor si, si
    mov si, ax   ; si = sectors to read
    
    pop dx       ; Restore drive in dl
    
    mov ch, 0    ; Start at cylinder 0
    mov dh, 0    ; Start at head 0
    ; cl = start sector (passed in)
    ; dl = drive (passed in)

.loop:
    test si, si
    jz .done

    mov ah, 0x02
    mov al, 1    ; Read 1 sector
    
    pusha        ; Save BIOS parameters/state
    int 0x13
    jc disk_error
    popa

    dec si
    
    ; Increment buffer pointer via Segment to avoid 64KB wrap
    ; 512 bytes = 0x200 bytes = 0x20 paragraphs
    mov ax, es
    add ax, 0x20
    mov es, ax
    
    inc cl       ; Next sector
    cmp cl, 19   ; 1.44MB Floppy has 18 sectors/track
    jne .loop
    
    mov cl, 1    ; Track wrap: back to sector 1
    inc dh       ; Next head
    cmp dh, 2    ; 1.44MB Floppy has 2 heads
    jne .loop
    
    mov dh, 0    ; Cylinder wrap: back to head 0
    inc ch       ; Next cylinder
    jmp .loop

.done:
    pop es
    popa
    ret

; disk_load_large: ES:BX = target, SI = sectors (16-bit), DL = drive, CL = start_sector
; Like disk_load but takes sector count via SI for >255 sector support.
disk_load_large:
    pusha
    push es

    ; SI already has the sector count (16-bit)
    ; DL = drive, CL = start sector
    mov ch, 0    ; Start at cylinder 0
    mov dh, 0    ; Start at head 0

.lloop:
    test si, si
    jz .ldone

    mov ah, 0x02
    mov al, 1    ; Read 1 sector

    pusha
    int 0x13
    jc disk_error
    popa

    dec si

    ; Advance buffer: segment += 0x20 (512 bytes)
    mov ax, es
    add ax, 0x20
    mov es, ax

    inc cl       ; Next sector
    cmp cl, 19   ; 1.44MB Floppy: 18 sectors/track
    jne .lloop

    mov cl, 1    ; Wrap: back to sector 1
    inc dh       ; Next head
    cmp dh, 2    ; 1.44MB Floppy: 2 heads
    jne .lloop

    mov dh, 0    ; Wrap: back to head 0
    inc ch       ; Next cylinder
    jmp .lloop

.ldone:
    pop es
    popa
    ret

disk_error:
    mov bx, DISK_ERROR
    call print
    call print_nl
    mov dh, ah
    call print_hex
    jmp disk_loop

sectors_error:
    mov bx, SECTORS_ERROR
    call print

disk_loop:
    jmp $

DISK_ERROR: db "Disk read error", 0
SECTORS_ERROR: db "Incorrect sectors read", 0
