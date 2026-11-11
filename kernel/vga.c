/*
 * SHENLONG OS - VGA Text Mode Sürücüsü (64-bit)
 */

#include "vga.h"
#include "kernel.h"

static uint16_t volatile *vga_buf = (uint16_t volatile *)0xB8000ULL;
static int cursor_row   = 0;
static int cursor_col   = 0;
static uint8_t cur_color = 0;

static uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)(fg | (bg << 4));
}

static uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static void update_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_init(void) {
    cur_color   = make_color(VGA_LIGHT_GREY, VGA_BLACK);
    cursor_row  = 0;
    cursor_col  = 0;
}

void vga_clear(void) {
    uint16_t blank = make_entry(' ', cur_color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buf[i] = blank;
    cursor_row = 0;
    cursor_col = 0;
    update_cursor();
}

void vga_color(vga_color_t fg, vga_color_t bg) {
    cur_color = make_color(fg, bg);
}

void vga_set_cursor(int row, int col) {
    cursor_row = row;
    cursor_col = col;
    update_cursor();
}

void vga_scroll(void) {
    for (int r = 1; r < VGA_HEIGHT; r++)
        for (int c = 0; c < VGA_WIDTH; c++)
            vga_buf[(r-1)*VGA_WIDTH+c] = vga_buf[r*VGA_WIDTH+c];
    uint16_t blank = make_entry(' ', cur_color);
    for (int c = 0; c < VGA_WIDTH; c++)
        vga_buf[(VGA_HEIGHT-1)*VGA_WIDTH+c] = blank;
    cursor_row = VGA_HEIGHT - 1;
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            vga_buf[cursor_row*VGA_WIDTH+cursor_col] = make_entry(' ', cur_color);
        }
    } else if (c == '\t') {
        int next = (cursor_col + 8) & ~7;
        while (cursor_col < next && cursor_col < VGA_WIDTH) {
            vga_buf[cursor_row*VGA_WIDTH+cursor_col] = make_entry(' ', cur_color);
            cursor_col++;
        }
    } else {
        vga_buf[cursor_row*VGA_WIDTH+cursor_col] = make_entry(c, cur_color);
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) { cursor_col = 0; cursor_row++; }
    if (cursor_row >= VGA_HEIGHT) vga_scroll();
    update_cursor();
}

void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}

void vga_put_hex(uint64_t val) {
    vga_puts("0x");
    char buf[17];
    buf[16] = '\0';
    for (int i = 15; i >= 0; i--) {
        int d = val & 0xF;
        buf[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        val >>= 4;
    }
    /* Önündeki sıfırları atla */
    char *p = buf;
    while (*p == '0' && *(p+1)) p++;
    vga_puts(p);
}

void vga_put_dec(int64_t val) {
    char buf[21];
    kitoa(val, buf);
    vga_puts(buf);
}