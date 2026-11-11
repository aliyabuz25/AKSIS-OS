; SHENLONG OS - Wallpaper disk imajı (gömülü)
section .rodata align=16
global wallpaper_img_data
global wallpaper_img_size

wallpaper_img_data:
    incbin "build/out/wallpapers.img"
wallpaper_img_data_end:

section .data align=4
wallpaper_img_size:
    dd wallpaper_img_data_end - wallpaper_img_data

section .note.GNU-stack noalloc noexec nowrite progbits