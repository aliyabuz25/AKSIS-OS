; =============================================================================
; SHENLONG OS - Kernel Giriş Noktası (Limine Boot Protocol)
; Limine kernel'i zaten 64-bit long mode'da başlatıyor
; Segment registerleri, page tables, A20 — hepsi Limine tarafından ayarlandı
; =============================================================================

[BITS 64]
[GLOBAL kernel_entry]
[EXTERN kernel_main]
[EXTERN bss_start]
[EXTERN bss_end]

section .text

kernel_entry:
    ; Stack kur
    cli
    lea rsp, [rel stack_top]

    ; Yönlendirme bayrağını temizle
    cld

    ; The MPEG decoder uses SSE2 floating point on this single-threaded kernel.
    mov rax, cr0
    and rax, ~((1 << 2) | (1 << 3))
    or rax, (1 << 1)
    mov cr0, rax
    mov rax, cr4
    or rax, (1 << 9) | (1 << 10)
    mov cr4, rax
    fninit
    ldmxcsr [rel initial_mxcsr]

    ; BSS sıfırla
    lea rdi, [rel bss_start]
    lea rcx, [rel bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb

    ; Kernel main'e atla
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .rodata
    align 4
initial_mxcsr: dd 0x1f80

section .bss
    ; QuickJS has a 128 KiB stack limit; leave room for its native caller.
    align 16
    resb 524288
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
