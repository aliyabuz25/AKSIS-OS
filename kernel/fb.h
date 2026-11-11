/*
 * SHENLONG OS - GPU-style Framebuffer
 * ANSI renk, per-cell color, grayscale outline glyphs, GPU primitives
 */

#ifndef FB_H
#define FB_H

#include "kernel.h"
#include "gpu.h"

/* ------------------------------------------------------------------ */
/* Renk paletleri (0x00RRGGBB)                                        */
/* ------------------------------------------------------------------ */
#define FB_BLACK        0x00000000
#define FB_WHITE        0x00FFFFFF
#define FB_CYAN         0x0000FFFF
#define FB_GREEN        0x0000FF00
#define FB_YELLOW       0x00FFFF00
#define FB_RED          0x00FF0000
#define FB_LIGHT_GREY   0x00C0C0C0
#define FB_DARK_GREY    0x00404040
#define FB_LIGHT_GREEN  0x0090EE90
#define FB_LIGHT_CYAN   0x0090FFFF
#define FB_ORANGE       0x00FF8C00
#define FB_MAGENTA      0x00FF00FF
#define FB_BLUE         0x000070FF
#define FB_DIM          0x00888888

/* Terminal tema - Smooth Light */
#define TERM_FG         0x001E293B
#define TERM_BG         0x00F8FAFC
#define TERM_CURSOR     0x000284C7

/* ------------------------------------------------------------------ */
/* Font boyutu — 10x24 native grayscale cells                       */
/* ------------------------------------------------------------------ */
#define FONT_W  10
#define FONT_H  24

/* ------------------------------------------------------------------ */
/* Temel framebuffer API                                              */
/* ------------------------------------------------------------------ */
void fb_init(void *addr, uint32_t width, uint32_t height,
             uint32_t pitch, uint16_t bpp);
void fb_clear(uint32_t color);
void fb_text_move(int delta);
void fb_putc(char c);
void fb_puts(const char *s);
void fb_puts_color(const char *s, uint32_t fg);
void fb_newline(void);
void fb_scroll(void);
void fb_text_cursor(int enabled);
void fb_set_fg(uint32_t color);
void fb_set_bg(uint32_t color);
void fb_set_viewport(int32_t x, int32_t y, uint32_t width, uint32_t height);
void fb_present(void);
void fb_window(int x, int y, uint32_t w, uint32_t h, int z, int visible);
void fb_window_alpha(int alpha);
void fb_window_snapshot(gpu_surface_t *dst,int x,int y);

/* ------------------------------------------------------------------ */
/* GPU primitives                                                     */
/* ------------------------------------------------------------------ */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_gradient_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t c0, uint32_t c1, int vertical);
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* ------------------------------------------------------------------ */
/* ANSI escape — "\x1b[31m" gibi diziler fb_puts ile işlenir         */
/* ------------------------------------------------------------------ */
/* Özel renk sıfırlama: "\x1b[0m"                                    */

/* ------------------------------------------------------------------ */
/* Output capture — pipe / redirect için                              */
/* ------------------------------------------------------------------ */
void     fb_capture_start(char *buf, uint32_t size);
void     fb_capture_end(void);
uint32_t fb_capture_len(void);

/* ------------------------------------------------------------------ */
/* Mevcut satır/sütun                                                 */
/* ------------------------------------------------------------------ */
extern int fb_row;
extern int fb_col;

/* Framebuffer değişkenleri */
extern uint8_t  *fb_addr;
extern uint32_t  fb_width;
extern uint32_t  fb_height;
extern uint32_t  fb_pitch;
extern uint32_t  fb_bpx;

#endif /* FB_H */
