/*
 * SHENLONG OS - WPF (Windows Presentation Foundation)
 * Retained-mode modern UI sistemi, Windows 10 Live Tiles, Fluent Design ve animasyon motoru.
 */
#ifndef SHENLONG_WPF_H
#define SHENLONG_WPF_H

#include "kernel.h"
#include "gpu.h"

typedef struct {
    int         app_id;
    const char *title;
    const char *subtitle;
    int         icon_idx;
    uint32_t    accent_color;
    int         wide;       /* 0=orta kare (1x1), 1=geniş karo (2x1) */
    int         flip_phase; /* Animasyon durumu */
} wpf_live_tile_t;

typedef struct {
    int         x, y, w, h;
    const char *text;
    uint32_t    accent;
    int         hovered;
    int         pressed;
    int         corner_radius;
} wpf_fluent_button_t;

/* WPF API */
void     wpf_init(void);

/* Animasyon motoru: 0..256 aralığında yumuşak cubic ease değeri döner */
uint32_t wpf_ease_in_out(uint32_t elapsed_ms, uint32_t total_duration_ms);

/* Fluent Design & Live Tile çizimleri */
void     wpf_render_live_tile(gpu_surface_t *s, const wpf_live_tile_t *tile,
                              int x, int y, int w, int h, int hovered);
void     wpf_render_tile_grid(gpu_surface_t *s, int base_x, int base_y,
                              int grid_w, int grid_h, int hovered_index);
void     wpf_render_fluent_button(gpu_surface_t *s, int x, int y, int w, int h,
                                  const char *label, int hovered, int pressed, uint32_t accent);
void     wpf_render_acrylic_card(gpu_surface_t *s, int x, int y, int w, int h,
                                 uint32_t tint, int radius);

#endif /* SHENLONG_WPF_H */
