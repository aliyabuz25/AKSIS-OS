; =============================================================================
; SHENLONG OS - Stage 2
; boot_cd bizi 0x1000:0x0200 = lineer 0x10200 adresine atlar
; Kernel boot image icinde seek=17 * 512 = 8704 byte offsette
; boot_cd boot image'i 0x10000'e yukledi, kernel 0x10000+8704=0x12200'de
; =============================================================================

[BITS 16]
[ORG 0x10200]

%define KERNEL_SRC  0x12200     ; bellekte kernel baslangici
%define KERNEL_DST  0x200000    ; kernel yuklenecek adres
%define KERNEL_SIZE (512*1024)  ; max 512KB kopyala

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0xFFFC
    sti

    mov si, msg_s2
    call print16

    ; A20
    call enable_a20

    ; CPUID long mode kontrolu
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no64
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)
    jz .no64
    jmp .ok64
.no64:
    mov si, msg_no64
    call print16
    jmp halt16
.ok64:
    mov si, msg_paging
    call print16

    ; ---- Page tables ----
    ; PML4 @ 0x1000, PDPT @ 0x2000, PD @ 0x3000
    ; Tablolari sifirla (0x1000 - 0x3FFF)
    mov edi, 0x1000
    mov cr3, edi
    xor eax, eax
    mov ecx, (3 * 4096) / 4
    rep stosd

    ; PML4[0] -> PDPT
    mov dword [0x1000], 0x2003   ; present+writable
    mov dword [0x1004], 0

    ; PDPT[0] -> PD
    mov dword [0x2000], 0x3003
    mov dword [0x2004], 0

    ; PD: 512 x 2MB hugepages
    mov edi, 0x3000
    mov ebx, 0x00000083         ; present+writable+huge
    mov ecx, 512
.pd:
    mov dword [edi],   ebx
    mov dword [edi+4], 0
    add ebx, 0x200000
    add edi, 8
    loop .pd

    ; ---- Unreal mode: kernel'i 0x200000'e kopyala ----
    mov si, msg_copy
    call print16

    ; Gecici GDT yukle (32-bit segment icin)
    lgdt [gdt_tmp_ptr]

    ; Protected mode ac
    mov eax, cr0
    or  eax, 1
    mov cr0, eax

    ; 32-bit data segment sec
    mov ax, 0x08
    mov ds, ax
    mov es, ax

    ; Paging ve PE kapat (unreal mode: sadece segment limit genisletildi)
    and eax, ~1
    mov cr0, eax

    ; Segment registerlar sifirla (ama limit 4GB olarak kalir = unreal mode)
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; rep movsd ile 32-bit adresler kullan
    mov esi, KERNEL_SRC
    mov edi, KERNEL_DST
    mov ecx, KERNEL_SIZE / 4
    a32 rep movsd

    mov si, msg_lmode
    call print16

    ; ---- Long mode gecisi ----
    lgdt [gdt64_ptr]

    ; PAE
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; EFER.LME
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; PG + PE
    mov eax, cr0
    or  eax, (1 << 0) | (1 << 31)
    mov cr0, eax

    ; 64-bit code segment'e atla
    jmp 0x08:long_mode_start

; ============================================================
; 16-bit yardimci rutinler
; ============================================================
enable_a20:
    call .wi
    mov al, 0xAD
    out 0x64, al
    call .wi
    mov al, 0xD0
    out 0x64, al
    call .wo
    in  al, 0x60
    push eax
    call .wi
    mov al, 0xD1
    out 0x64, al
    call .wi
    pop eax
    or  al, 0x02
    out 0x60, al
    call .wi
    mov al, 0xAE
    out 0x64, al
    call .wi
    ret
.wi:
    in  al, 0x64
    test al, 0x02
    jnz .wi
    ret
.wo:
    in  al, 0x64
    test al, 0x01
    jz  .wo
    ret

print16:
    push ax
    push bx
.lp:
    lodsb
    test al, al
    jz .dn
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    jmp .lp
.dn:
    pop bx
    pop ax
    ret

halt16:
    cli
    hlt
    jmp halt16

; ============================================================
; Gecici GDT (unreal mode icin - 32-bit data segment)
; ============================================================
align 4
gdt_tmp:
    dq 0x0000000000000000       ; null
    dq 0x00CF92000000FFFF       ; 32-bit data, base=0, limit=4GB
gdt_tmp_end:
gdt_tmp_ptr:
    dw gdt_tmp_end - gdt_tmp - 1
    dd gdt_tmp                   ; 32-bit base (ORG'dan hesaplaniyor)

; ============================================================
; 64-bit GDT
; ============================================================
align 4
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF       ; 64-bit kod (L=1)
    dq 0x00AF92000000FFFF       ; 64-bit veri
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64                     ; 32-bit base

; ============================================================
; Mesajlar
; ============================================================
msg_s2:    db 'Stage2: Baslatiliyor', 0x0D, 0x0A, 0
msg_paging:db 'Sayfalama tablosu kuruluyor...', 0x0D, 0x0A, 0
msg_copy:  db 'Kernel kopyalaniyor...', 0x0D, 0x0A, 0
msg_lmode: db 'Long mode geciliyor...', 0x0D, 0x0A, 0
msg_no64:  db 'HATA: 64-bit destegi yok!', 0x0D, 0x0A, 0

; ============================================================
; 64-bit Long Mode Entry
; ============================================================
[BITS 64]
long_mode_start:
    ; Veri segmentlerini ayarla
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x9FC00

    ; VGA'ya "64-bit OK" yaz (satir 4)
    mov rbx, 0xB8000 + (4 * 80 * 2)
    lea rsi, [rel msg_ok64]
.pr:
    lodsb
    test al, al
    jz .pd
    mov [rbx], al
    mov byte [rbx+1], 0x0A   ; yesil
    add rbx, 2
    jmp .pr
.pd:
    ; Kernel'e atla
    mov rax, KERNEL_DST
    jmp rax

msg_ok64: db '64-bit Long Mode OK! Kernel calistiriliyor...', 0

; padding
times 8192 - ($ - $$) db 0
