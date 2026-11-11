; =============================================================================
; SHENLONG OS - CD-ROM Bootloader (El Torito, No Emulation)
; BIOS bu 512-byte bloğu 0x7C00'a yükler.
; Tüm boot image'ı 0x10000 adresine okur, stage2'ye 0x10200'de atlar.
; Boot image yapısı:
;   [0x000 - 0x1FF] boot_cd.bin   (512 byte)  -> bu kod
;   [0x200 - 0x21FF] stage2.bin   (8192 byte)
;   [0x2200 - ...  ] kernel.bin   (değişken)
; =============================================================================

[BITS 16]
[ORG 0x7C00]

%define LOAD_SEG    0x1000      ; boot image buraya yuklenecek (0x10000)
%define STAGE2_OFF  0x0200      ; stage2 boot image icinde offset 512

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    ; Ekrani temizle
    mov ax, 0x0003
    int 0x10

    mov si, msg_boot
    call print_str

    ; int 13h extension kontrolu
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .no_ext
    cmp bx, 0xAA55
    jne .no_ext
    jmp .ok_ext

.no_ext:
    mov si, msg_noext
    call print_str
    jmp halt

.ok_ext:
    ; Tum boot image'i oku: LBA 0'dan itibaren
    ; Boot image boyutu: 512 + 8192 + kernel (~100KB) = ~110KB
    ; 110KB / 512 = 215 sektor, yukarı yuvarlayalım 256 sektor (128KB)
    ; CD sektoru = 2048 byte = 4 x 512-byte blok
    ; 256 / 4 = 64 CD sektoru

    ; DAP ile 256 adet 512-byte blok oku
    mov word [dap_count],   256
    mov word [dap_buf_seg], LOAD_SEG
    mov word [dap_buf_off], 0x0000
    mov dword [dap_lba_lo], 0
    mov dword [dap_lba_hi], 0

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc .disk_err

    mov si, msg_ok
    call print_str

    ; stage2'ye atla: LOAD_SEG:STAGE2_OFF = 0x1000:0x0200
    mov dl, [boot_drive]
    jmp LOAD_SEG:STAGE2_OFF

.disk_err:
    mov si, msg_err
    call print_str
    jmp halt

; --- DAP ---
dap:
    db  0x10
    db  0x00
dap_count:
    dw  256
dap_buf_off:
    dw  0x0000
dap_buf_seg:
    dw  LOAD_SEG
dap_lba_lo:
    dd  0
dap_lba_hi:
    dd  0

; --- print_str ---
print_str:
    push ax
    push bx
.lp:
    lodsb
    test al, al
    jz .dn
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp .lp
.dn:
    pop bx
    pop ax
    ret

halt:
    cli
    hlt
    jmp halt

boot_drive: db 0
msg_boot:   db 'SHENLONG OS - CD Boot', 0x0D, 0x0A, 0
msg_ok:     db 'Image yuklendi. Stage2 baslatiliyor...', 0x0D, 0x0A, 0
msg_noext:  db 'HATA: int13h ext yok!', 0x0D, 0x0A, 0
msg_err:    db 'HATA: Disk okuma hatasi!', 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55
