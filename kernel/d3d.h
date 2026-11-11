/*
 * SHENLONG OS - Direct3D Pipeline Abstraction
 * Hızlandırılmış 2D/3D rasterizasyon, Acrylic/Blur efektleri ve gölgeler.
 */
#ifndef SHENLONG_D3D_H
#define SHENLONG_D3D_H

#include "kernel.h"
#include "gpu.h"

typedef struct {
    uint32_t *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
} d3d_texture_t;

typedef struct {
    d3d_texture_t *render_target;
    uint32_t       viewport_w;
    uint32_t       viewport_h;
    uint32_t       blend_mode;
    uint8_t        global_alpha;
} d3d_device_t;

extern d3d_device_t g_d3d_device;

/* Direct3D API */
void d3d_init(uint32_t screen_w, uint32_t screen_h);
void d3d_set_render_target(d3d_texture_t *target);
void d3d_clear(d3d_texture_t *tex, uint32_t color);
void d3d_fill_rect(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t color);
void d3d_draw_rect(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t color, int thickness);
void d3d_gradient_v(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t c_top, uint32_t c_bot);
void d3d_gradient_h(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t c_left, uint32_t c_right);
void d3d_blit(d3d_texture_t *dst, int dx, int dy, const d3d_texture_t *src, int sx, int sy, int w, int h);
void d3d_blend_copy(uint32_t *dst, const uint32_t *src, uint32_t count, uint8_t alpha);
void d3d_acrylic_glass(d3d_texture_t *dst, const d3d_texture_t *backdrop,
                       int x, int y, int w, int h, uint32_t tint, int radius);
void d3d_drop_shadow(d3d_texture_t *dst, int x, int y, int w, int h, int radius, uint8_t max_alpha);
void d3d_rounded_rect(d3d_texture_t *tex, int x, int y, int w, int h, int radius, uint32_t color);

#endif /* SHENLONG_D3D_H */
