bits 64
section .rodata

global panic_logo_data
global panic_logo_size

panic_logo_data:
    incbin "build/out/panic-logo.rgb"
panic_logo_data_end:

panic_logo_size:
    dd panic_logo_data_end - panic_logo_data
