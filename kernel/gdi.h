/*
 * SHENLONG OS - GDI (Graphics Device Interface)
 * Klasik 2D çizim, metin, kalem, fırça, BitBlt ve temel kontroller.
 */
#ifndef SHENLONG_GDI_H
#define SHENLONG_GDI_H

#include "kernel.h"
#include "gpu.h"

typedef uint32_t COLORREF;

#define RGB(r,g,b) ((COLORREF)(((uint8_t)(r)|((uint16_t)((uint8_t)(g))<<8))|(((uint32_t)((uint8_t)(b)))<<16)))
#define RGBA(r,g,b,a) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define GetRValue(rgb) (((rgb) >> 16) & 0xFF)
#define GetGValue(rgb) (((rgb) >> 8) & 0xFF)
#define GetBValue(rgb) ((rgb) & 0xFF)

#define PS_SOLID       0
#define PS_DASH        1
#define BS_SOLID       0
#define BS_HOLLOW      1

typedef struct {
    int      style;
    int      width;
    uint32_t color;
} HPEN;

typedef struct {
    int      style;
    uint32_t color;
} HBRUSH;

typedef struct {
    gpu_surface_t *surface;
    HPEN           current_pen;
    HBRUSH         current_brush;
    int            cur_x;
    int            cur_y;
    uint32_t       text_color;
    uint32_t       bk_color;
    int            bk_mode; /* 0=transparent, 1=opaque */
} HDC_STRUCT, *HDC;

/* GDI Temel Fonksiyonları */
HDC    gdi_create_dc(gpu_surface_t *surface);
void   gdi_delete_dc(HDC hdc);
HPEN   gdi_create_pen(int style, int width, uint32_t color);
HBRUSH gdi_create_solid_brush(uint32_t color);
HPEN   gdi_select_pen(HDC hdc, HPEN pen);
HBRUSH gdi_select_brush(HDC hdc, HBRUSH brush);
void   gdi_set_text_color(HDC hdc, uint32_t color);

/* GDI Çizim Primitifleri */
void   gdi_move_to(HDC hdc, int x, int y);
void   gdi_line_to(HDC hdc, int x, int y);
void   gdi_rectangle(HDC hdc, int left, int top, int right, int bottom);
void   gdi_round_rect(HDC hdc, int left, int top, int right, int bottom, int rx, int ry);
void   gdi_fill_rect(HDC hdc, int left, int top, int right, int bottom, HBRUSH brush);
void   gdi_draw_text(HDC hdc, int x, int y, const char *text, uint32_t color);
void   gdi_bitblt(HDC dst, int dx, int dy, int w, int h, HDC src, int sx, int sy);
void   gdi_alpha_blend(HDC dst, int dx, int dy, int w, int h, HDC src, int sx, int sy, uint8_t alpha);

/* GDI Temel Kontroller */
#define GDI_BTN_NORMAL  0
#define GDI_BTN_HOVER   1
#define GDI_BTN_PRESSED 2
#define GDI_BTN_DEFAULT 3

void   gdi_draw_button(HDC hdc, int x, int y, int w, int h, const char *label, int state);
void   gdi_draw_checkbox(HDC hdc, int x, int y, int checked, const char *label);
void   gdi_draw_progress_bar(HDC hdc, int x, int y, int w, int h, int percent);
void   gdi_draw_group_box(HDC hdc, int x, int y, int w, int h, const char *title);

#endif /* SHENLONG_GDI_H */
