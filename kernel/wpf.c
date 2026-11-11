/*
 * SHENLONG OS - WPF (Windows Presentation Foundation) Implementation
 * Retained-mode modern UI sistemi, Windows 10 Live Tiles ve Fluent Design.
 */
#include "wpf.h"
#include "gpu.h"
#include "ui_font.h"
#include "ui_paint.h"
#include "icons.h"
#include "d3d.h"

static const wpf_live_tile_t s_default_tiles[] = {
    { 3, "Jade Tarayıcı", "Web'de Gezin", 3, 0xFF0078D7u, 0, 0 },
    { 1, "Dosyalar",     "Depolama Hazır", 1, 0xFFD83B01u, 0, 0 },
    { 2, "Ayarlar",      "AXIS v6.0.2",   2, 0xFF0063B1u, 0, 0 },
    { 0, "Terminal",     "x86_64 Kabuk",   0, 0xFF107C41u, 0, 0 },
    { 5, "Medya",        "Müzik & Video",  5, 0xFFD13438u, 0, 0 },
    { 4, "Ağ Durumu",    "Gigabit LAN",    4, 0xFF008272u, 0, 0 }
};

void wpf_init(void) {
}

/* 0..256 aralığında cubic ease-in/out */
uint32_t wpf_ease_in_out(uint32_t elapsed_ms, uint32_t total_duration_ms) {
    if (total_duration_ms == 0 || elapsed_ms >= total_duration_ms) return 256;
    uint32_t t = (elapsed_ms * 256) / total_duration_ms; /* 0..256 */

    if (t < 128) {
        /* 4 * t^3 */
        uint32_t t2 = (t * t) >> 8;
        return (4 * t2 * t) >> 8;
    } else {
        uint32_t inv = 256 - t;
        uint32_t inv2 = (inv * inv) >> 8;
        uint32_t val = (4 * inv2 * inv) >> 8;
        return 256 - val;
    }
}

static inline uint32_t brighten(uint32_t color, int percent) {
    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;
    r = r + ((255 - r) * percent) / 100;
    g = g + ((255 - g) * percent) / 100;
    b = b + ((255 - b) * percent) / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (0xFF000000u) | (r << 16) | (g << 8) | b;
}

void wpf_render_live_tile(gpu_surface_t *s, const wpf_live_tile_t *tile,
                          int x, int y, int w, int h, int hovered) {
    if (!s || !tile || w <= 0 || h <= 0) return;

    uint32_t bg = hovered ? brighten(tile->accent_color, 18) : tile->accent_color;
    gpu_fill_rect(s, x, y, w, h, bg);

    if (hovered) {
        /* Windows 10 Fluent Reveal Highlight kenarlık */
        gpu_draw_rect(s, x, y, w, h, 0xFFFFFFFFu, 1);
    }

    /* Simge (üst-sol) */
    ui_icon(s, x + 16, y + 16, tile->icon_idx, 32);

    /* Başlık (alt-sol) */
    ui_text_fit(s, x + 14, y + h - 34, w - 24, tile->title, 0xFFFFFFFFu, 1);

    /* Alt bilgi */
    if (tile->subtitle && tile->subtitle[0] && h >= 100) {
        ui_text_fit(s, x + 14, y + h - 18, w - 24, tile->subtitle, 0xFFE0E0E0u, 0);
    }
}

void wpf_render_tile_grid(gpu_surface_t *s, int base_x, int base_y,
                          int grid_w, int grid_h, int hovered_index) {
    (void)grid_h;
    if (!s || grid_w <= 0) return;

    int cols = 2;
    int tile_w = (grid_w - 20) / cols;
    if (tile_w > 160) tile_w = 160;
    int tile_h = 100;
    int gap = 8;

    for (int i = 0; i < 6; i++) {
        int col = i % cols;
        int row = i / cols;
        int tx = base_x + col * (tile_w + gap);
        int ty = base_y + row * (tile_h + gap);
        int hot = (hovered_index == i);

        wpf_render_live_tile(s, &s_default_tiles[i], tx, ty, tile_w, tile_h, hot);
    }
}

void wpf_render_fluent_button(gpu_surface_t *s, int x, int y, int w, int h,
                              const char *label, int hovered, int pressed, uint32_t accent) {
    if (!s || w <= 0 || h <= 0) return;
    uint32_t bg = pressed ? 0xFF1A1A1Au : (hovered ? 0xFF383838u : 0xFF2B2B2Bu);
    uint32_t border = hovered ? (accent ? accent : 0xFF0078D7u) : 0xFF4A4A4Au;

    gpu_fill_rect(s, x, y, w, h, bg);
    gpu_draw_rect(s, x, y, w, h, border, 1);

    if (label && label[0]) {
        ui_text_fit(s, x + 12, y + (h - 14) / 2, w - 24, label, 0xFFFFFFFFu, 0);
    }
}

void wpf_render_acrylic_card(gpu_surface_t *s, int x, int y, int w, int h,
                             uint32_t tint, int radius) {
    if (!s || w <= 0 || h <= 0) return;
    ui_round(s, x, y, w, h, radius, tint);
    gpu_draw_rect(s, x, y, w, h, 0xFF555555u, 1);
}
