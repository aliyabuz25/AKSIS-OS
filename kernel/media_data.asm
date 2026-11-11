section .rodata align=16
global media_demo_data,media_demo_end
global media_bieber_data,media_bieber_end

media_demo_data:
    incbin "assets/media/demo.mpg"
media_demo_end:

media_bieber_data:
    incbin "assets/media/bieber.mpg"
media_bieber_end:

section .note.GNU-stack noalloc noexec nowrite progbits
