/*
 * SHENLONG OS - VGA Text Mode Sürücüsü Header (64-bit)
 */

#ifndef VGA_H
#define VGA_H

#include "kernel.h"

typedef enum {
    VGA_BLACK         = 0,
    VGA_BLUE          = 1,
    VGA_GREEN         = 2,
    VGA_CYAN          = 3,
    VGA_RED           = 4,
    VGA_MAGENTA       = 5,
    VGA_BROWN         = 6,
    VGA_LIGHT_GREY    = 7,
    VGA_DARK_GREY     = 8,
    VGA_LIGHT_BLUE    = 9,
    VGA_LIGHT_GREEN   = 10,
    VGA_LIGHT_CYAN    = 11,
    VGA_LIGHT_RED     = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_YELLOW        = 14,
    VGA_WHITE         = 15,
} vga_color_t;

#define VGA_ADDR        ((uint16_t volatile *)0xB8000ULL)
#define VGA_WIDTH       80
#define VGA_HEIGHT      25

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_color(vga_color_t fg, vga_color_t bg);
void vga_set_cursor(int row, int col);
void vga_scroll(void);
void vga_put_hex(uint64_t val);
void vga_put_dec(int64_t val);

#endif /* VGA_H */
