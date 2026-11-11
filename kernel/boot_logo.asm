; The original logo is embedded once, with no filesystem dependency at boot.
section .rodata align=16
global boot_logo_data
boot_logo_data:
    incbin "build/out/boot-logo.rgb"
section .note.GNU-stack noalloc noexec nowrite progbits
