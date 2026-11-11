; =============================================================================
; SHENLONG OS - Stage 1 Bootloader (MBR)
; 512 bytes, BIOS tarafindan 0x7C00 adresine yuklenir
; =============================================================================

[BITS 16]
[ORG 0x7C00]

%define STAGE2_ADDR     0x0500      ; Stage2'nin yukleneceği adres
%define STAGE2_SECTOR   2           ; Stage2 hangi sektorden baslar
%define STAGE2_SIZE     16          ; Stage2 kac sektor

start:
    ; Segment registerları sıfırla
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Boot drive numarasını kaydet
    mov [boot_drive], dl

    ; Ekranı temizle
    mov ax, 0x0003
    int 0x10

    ; Baslik yazdir
    mov si, msg_loading
    call print_string

    ; Stage2'yi diskten oku
    mov ax, STAGE2_ADDR >> 4
    mov es, ax
    xor bx, bx

    mov ah, 0x02            ; BIOS read sectors
    mov al, STAGE2_SIZE     ; sektor sayisi
    mov ch, 0               ; cylinder
    mov cl, STAGE2_SECTOR   ; baslangic sektoru
    mov dh, 0               ; head
    mov dl, [boot_drive]    ; drive
    int 0x13

    jc disk_error           ; carry flag = hata

    ; Stage2'ye atla
    mov si, msg_ok
    call print_string

    mov dl, [boot_drive]
    jmp STAGE2_ADDR

disk_error:
    mov si, msg_error
    call print_string
    jmp halt

; -----------------------------------------------------------------------------
; print_string: SI registerındaki null-terminated stringi yazar
; -----------------------------------------------------------------------------
print_string:
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
; Veriler
; -----------------------------------------------------------------------------
boot_drive:     db 0
msg_loading:    db 'SHENLONG OS Bootloader v1.0', 0x0D, 0x0A
                db 'Loading Stage2...', 0x0D, 0x0A, 0
msg_ok:         db 'Stage2 loaded. Jumping...', 0x0D, 0x0A, 0
msg_error:      db 'DISK READ ERROR!', 0x0D, 0x0A, 0

; -----------------------------------------------------------------------------
; Boot imzasi (510. ve 511. byte)
; -----------------------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55
