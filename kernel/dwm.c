/*
 * SHENLONG OS - DWM (Desktop Window Manager) Implementation
 * Retained pencere birleştirici, Fluent efektleri ve dinamik ekran boyutu adaptasyonu.
 */
#include "dwm.h"
#include "d3d.h"
#include "gpu.h"
#include "fb.h"
#include "ui_font.h"
#include "ui_paint.h"
#include "kernel_info.h"

extern void ui_on_display_resize(uint32_t new_w, uint32_t new_h);

void dwm_init(void *fb_addr, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp) {
    (void)fb_addr; (void)pitch; (void)bpp;
    d3d_init(w, h);
}

void dwm_compose(void) {
    gpu_compose();
}

void dwm_damage(int x, int y, int w, int h) {
    gpu_damage(x, y, w, h);
}

void dwm_draw_window_frame(gpu_surface_t *s, int w, int h, const char *title, int active, int hovered_btn) {
    if (!s || !s->pixels || w <= 0 || h <= 0) return;

    int radius = 12;

    /* Başlık çubuğu alanı temizle (straight-alpha için) */
    for (int y = 0; y < 44; y++) {
        for (int x = 0; x < w; x++) s->pixels[y * s->stride + x] = 0;
    }

    /* Buzlu cam / yarı saydam başlık çubuğu (44px) - Akıcı Light Tema */
    uint32_t title_bg = active ? 0xeef8fafcu : 0xdeedf2f7u;
    ui_round_top(s, 0, 0, w, 44, radius, title_bg);

    /* Üst cam yansıması gradient */
    for (int y = 1; y < 16; y++) {
        uint32_t ga = (active ? 28 : 16) - (y * (active ? 26 : 15) / 16);
        if (ga > 0) {
            uint32_t gloss_col = (ga << 24) | 0x00FFFFFFu;
            int inset = (y < radius) ? (radius - y) : 0;
            int line_w = w - inset * 2;
            if (line_w > 0) gpu_fill_rect(s, inset, y, line_w, 1, gloss_col);
        }
    }

    /* Üst ve kenar cam vurguları */
    uint32_t top_sheen = active ? 0x90ffffffu : 0x50ffffffu;
    uint32_t side_sheen = active ? 0x50cbd5e1u : 0x30cbd5e1u;
    gpu_fill_rect(s, radius, 0, w - radius * 2, 1, top_sheen);
    gpu_fill_rect(s, 0, radius, 1, 44 - radius, side_sheen);
    gpu_fill_rect(s, w - 1, radius, 1, 44 - radius, side_sheen);

    /* Alt ayırıcı çizgi */
    gpu_fill_rect(s, 0, 43, w, 1, 0x14000000u);
    gpu_fill_rect(s, 0, 44, w, 1, 0x18000000u);

    /* Başlık metni */
    if (title && title[0]) {
        ui_text_fit(s, 44, 14, w - 180, title, active ? 0xFF0F172Au : 0xFF64748Bu, 0);
    }

    /* Sağ üst pencere kontrolleri: modern hap / pill butonlar [ - ] [ □ ] [ X ] */
    for (int action = 0; action < 3; action++) {
        int rx = w - 138 + 46 * action;
        int ry = 1;
        int rw = 46;
        int rh = 42;
        int hot = (hovered_btn == action);

        if (hot) {
            uint32_t btn_bg = (action == 2) ? 0xe0e11d48u : 0x20000000u;
            ui_round(s, rx + 3, ry + 6, rw - 6, rh - 12, 6, btn_bg);
        }

        int cx = rx + rw / 2;
        int cy = 22;
        uint32_t ink = (hot && action == 2) ? 0xFFFFFFFFu : ((active || hot) ? 0xFF0F172Au : 0xFF64748Bu);

        if (action == 0) {
            /* Simge durumuna küçült: çizgi */
            gpu_fill_rect(s, cx - 5, cy, 10, 1, ink);
        } else if (action == 1) {
            /* Ekranı kapla: kare */
            gpu_draw_rect(s, cx - 5, cy - 5, 10, 10, ink, 1);
        } else if (action == 2) {
            /* Kapat: X */
            gpu_draw_line(s, cx - 5, cy - 5, cx + 5, cy + 5, ink);
            gpu_draw_line(s, cx + 5, cy - 5, cx - 5, cy + 5, ink);
        }
    }

    /* Pencere dış kenarlığı */
    uint32_t border_col = active ? 0x700284C7u : 0x40CBD5E1u;
    gpu_fill_rect(s, 0, 44, 1, h - 44 - radius, border_col);
    gpu_fill_rect(s, w - 1, 44, 1, h - 44 - radius, border_col);
    gpu_fill_rect(s, radius, h - 1, w - radius * 2, 1, border_col);
    ui_round_border_corners(s, w, h, radius, top_sheen, border_col);
}

void dwm_draw_drop_shadow(gpu_surface_t *shadow, int w, int h) {
    if (!shadow || !shadow->pixels) return;
    shadow->w = w + 48;
    shadow->h = h + 44;
    uint32_t count = shadow->stride * shadow->h;
    for (uint32_t i = 0; i < count; i++) shadow->pixels[i] = 0;

    int num_steps = 18;
    for (int i = 0; i < num_steps; i++) {
        int sx = i;
        int sy = i * 16 / 24 + 2;
        int sw = shadow->w - i * 2;
        int sh = shadow->h - i * 2 - 4;
        int rad = 30 - i;
        if (rad < 8) rad = 8;
        if (sw <= 0 || sh <= 0) break;

        uint32_t a = 3 + (i * 4) / num_steps;
        uint32_t col = (a << 24) | 0x000F172Au;
        ui_round(shadow, sx, sy, sw, sh, rad, col);
    }
}

void dwm_resize_display(uint32_t new_w, uint32_t new_h) {
    if (new_w < 640 || new_h < 480 || new_w > 3840 || new_h > 2160) return;

    /* Framebuffer global boyutlarını güncelle */
    gpu_fb_w     = new_w;
    gpu_fb_h     = new_h;
    gpu_fb_pitch = new_w * 4;

    fb_width     = new_w;
    fb_height    = new_h;
    fb_pitch     = new_w * 4;

    g_sysinfo.fb_width  = new_w;
    g_sysinfo.fb_height = new_h;

    /* D3D cihaz görünüm alanını güncelle */
    d3d_init(new_w, new_h);

    /* UI sistemini ekran boyutuna adapte et */
    ui_on_display_resize(new_w, new_h);

    /* Tüm ekranı hasarlı işaretle ve yeniden birleştir */
    gpu_damage(0, 0, new_w, new_h);
    gpu_compose();
}
