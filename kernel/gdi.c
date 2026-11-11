/*
 * SHENLONG OS - GDI (Graphics Device Interface) Implementation
 * Klasik 2D çizim, metin, kalem, fırça, BitBlt ve temel kontroller.
 */
#include "gdi.h"
#include "gpu.h"
#include "ui_font.h"
#include "ui_paint.h"

#define GDI_MAX_DCS 16
static HDC_STRUCT s_dc_pool[GDI_MAX_DCS];
static int        s_dc_used[GDI_MAX_DCS];

HDC gdi_create_dc(gpu_surface_t *surface) {
    if (!surface) return NULL;
    for (int i = 0; i < GDI_MAX_DCS; i++) {
        if (!s_dc_used[i]) {
            s_dc_used[i] = 1;
            HDC hdc = &s_dc_pool[i];
            hdc->surface = surface;
            hdc->current_pen = gdi_create_pen(PS_SOLID, 1, 0xFFFFFFFFu);
            hdc->current_brush = gdi_create_solid_brush(0xFF000000u);
            hdc->cur_x = 0;
            hdc->cur_y = 0;
            hdc->text_color = 0xFFFFFFFFu;
            hdc->bk_color = 0xFF000000u;
            hdc->bk_mode = 0;
            return hdc;
        }
    }
    return NULL;
}

void gdi_delete_dc(HDC hdc) {
    if (!hdc) return;
    for (int i = 0; i < GDI_MAX_DCS; i++) {
        if (&s_dc_pool[i] == hdc) {
            s_dc_used[i] = 0;
            break;
        }
    }
}

HPEN gdi_create_pen(int style, int width, uint32_t color) {
    HPEN p;
    p.style = style;
    p.width = (width > 0) ? width : 1;
    p.color = color;
    return p;
}

HBRUSH gdi_create_solid_brush(uint32_t color) {
    HBRUSH b;
    b.style = BS_SOLID;
    b.color = color;
    return b;
}

HPEN gdi_select_pen(HDC hdc, HPEN pen) {
    if (!hdc) return pen;
    HPEN old = hdc->current_pen;
    hdc->current_pen = pen;
    return old;
}

HBRUSH gdi_select_brush(HDC hdc, HBRUSH brush) {
    if (!hdc) return brush;
    HBRUSH old = hdc->current_brush;
    hdc->current_brush = brush;
    return old;
}

void gdi_set_text_color(HDC hdc, uint32_t color) {
    if (hdc) hdc->text_color = color;
}

void gdi_move_to(HDC hdc, int x, int y) {
    if (hdc) {
        hdc->cur_x = x;
        hdc->cur_y = y;
    }
}

void gdi_line_to(HDC hdc, int x, int y) {
    if (!hdc || !hdc->surface) return;
    gpu_draw_line(hdc->surface, hdc->cur_x, hdc->cur_y, x, y, hdc->current_pen.color);
    hdc->cur_x = x;
    hdc->cur_y = y;
}

void gdi_rectangle(HDC hdc, int left, int top, int right, int bottom) {
    if (!hdc || !hdc->surface) return;
    int w = right - left;
    int h = bottom - top;
    if (w <= 0 || h <= 0) return;

    if (hdc->current_brush.style == BS_SOLID) {
        gpu_fill_rect(hdc->surface, left, top, w, h, hdc->current_brush.color);
    }
    if (hdc->current_pen.width > 0) {
        gpu_draw_rect(hdc->surface, left, top, w, h, hdc->current_pen.color, hdc->current_pen.width);
    }
}

void gdi_round_rect(HDC hdc, int left, int top, int right, int bottom, int rx, int ry) {
    (void)ry;
    if (!hdc || !hdc->surface) return;
    int w = right - left;
    int h = bottom - top;
    if (w <= 0 || h <= 0) return;

    if (hdc->current_brush.style == BS_SOLID) {
        ui_round(hdc->surface, left, top, w, h, rx, hdc->current_brush.color);
    }
    if (hdc->current_pen.width > 0) {
        gpu_draw_rect(hdc->surface, left, top, w, h, hdc->current_pen.color, hdc->current_pen.width);
    }
}

void gdi_fill_rect(HDC hdc, int left, int top, int right, int bottom, HBRUSH brush) {
    if (!hdc || !hdc->surface) return;
    int w = right - left;
    int h = bottom - top;
    if (w <= 0 || h <= 0) return;
    gpu_fill_rect(hdc->surface, left, top, w, h, brush.color);
}

void gdi_draw_text(HDC hdc, int x, int y, const char *text, uint32_t color) {
    if (!hdc || !hdc->surface || !text) return;
    ui_text_fit(hdc->surface, x, y, (int)hdc->surface->w - x, text, color, 0);
}

void gdi_bitblt(HDC dst, int dx, int dy, int w, int h, HDC src, int sx, int sy) {
    if (!dst || !src || !dst->surface || !src->surface) return;
    gpu_blit(dst->surface, dx, dy, src->surface, sx, sy, w, h);
}

void gdi_alpha_blend(HDC dst, int dx, int dy, int w, int h, HDC src, int sx, int sy, uint8_t alpha) {
    if (!dst || !src || !dst->surface || !src->surface) return;
    for (int y = 0; y < h; y++) {
        int d_y = dy + y;
        int s_y = sy + y;
        if (d_y < 0 || d_y >= (int)dst->surface->h || s_y < 0 || s_y >= (int)src->surface->h) continue;
        for (int x = 0; x < w; x++) {
            int d_x = dx + x;
            int s_x = sx + x;
            if (d_x < 0 || d_x >= (int)dst->surface->w || s_x < 0 || s_x >= (int)src->surface->w) continue;
            uint32_t sp = src->surface->pixels[s_y * src->surface->stride + s_x];
            uint32_t *dp = &dst->surface->pixels[d_y * dst->surface->stride + d_x];
            uint32_t sa = (((sp >> 24) & 0xFF) * alpha) / 255;
            uint32_t blended = (sa << 24) | (sp & 0x00FFFFFFu);
            *dp = gpu_alpha_blend(*dp, blended);
        }
    }
}

void gdi_draw_button(HDC hdc, int x, int y, int w, int h, const char *label, int state) {
    if (!hdc || !hdc->surface || w <= 0 || h <= 0) return;
    uint32_t bg = 0xFF2D2D2Du;
    uint32_t border = 0xFF555555u;
    uint32_t text_col = 0xFFFFFFFFu;

    if (state == GDI_BTN_HOVER) {
        bg = 0xFF3E3E3Eu;
        border = 0xFF777777u;
    } else if (state == GDI_BTN_PRESSED) {
        bg = 0xFF1F1F1Fu;
        border = 0xFF999999u;
    } else if (state == GDI_BTN_DEFAULT) {
        bg = 0xFF0078D7u;
        border = 0xFF005A9Eu;
    }

    gpu_fill_rect(hdc->surface, x, y, w, h, bg);
    gpu_draw_rect(hdc->surface, x, y, w, h, border, 1);

    if (label && label[0]) {
        int text_x = x + 12;
        int text_y = y + (h - 14) / 2;
        ui_text_fit(hdc->surface, text_x, text_y, w - 24, label, text_col, 0);
    }
}

void gdi_draw_checkbox(HDC hdc, int x, int y, int checked, const char *label) {
    if (!hdc || !hdc->surface) return;
    int box_sz = 16;
    gpu_fill_rect(hdc->surface, x, y, box_sz, box_sz, 0xFF2D2D2Du);
    gpu_draw_rect(hdc->surface, x, y, box_sz, box_sz, 0xFF888888u, 1);

    if (checked) {
        /* Checkmark */
        gpu_draw_line(hdc->surface, x + 3, y + 8, x + 7, y + 12, 0xFF0078D7u);
        gpu_draw_line(hdc->surface, x + 7, y + 12, x + 13, y + 4, 0xFF0078D7u);
    }

    if (label && label[0]) {
        ui_text_fit(hdc->surface, x + box_sz + 8, y + 1, 200, label, 0xFFE0E0E0u, 0);
    }
}

void gdi_draw_progress_bar(HDC hdc, int x, int y, int w, int h, int percent) {
    if (!hdc || !hdc->surface || w <= 0 || h <= 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    gpu_fill_rect(hdc->surface, x, y, w, h, 0xFF222222u);
    gpu_draw_rect(hdc->surface, x, y, w, h, 0xFF444444u, 1);

    int fill_w = (w - 2) * percent / 100;
    if (fill_w > 0) {
        gpu_fill_rect(hdc->surface, x + 1, y + 1, fill_w, h - 2, 0xFF0078D7u);
    }
}

void gdi_draw_group_box(HDC hdc, int x, int y, int w, int h, const char *title) {
    if (!hdc || !hdc->surface || w <= 0 || h <= 0) return;
    gpu_draw_rect(hdc->surface, x, y + 7, w, h - 7, 0xFF444444u, 1);
    if (title && title[0]) {
        gpu_fill_rect(hdc->surface, x + 12, y, 100, 14, 0xFF202020u);
        ui_text_fit(hdc->surface, x + 16, y, 96, title, 0xFFB0B0B0u, 0);
    }
}
