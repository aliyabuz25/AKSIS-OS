/*
 * SHENLONG OS - Direct3D Pipeline Implementation
 * Hızlandırılmış rasterizasyon, Acrylic/Blur efektleri ve gölgeler.
 */
#include "d3d.h"

d3d_device_t g_d3d_device;

static inline int d3d_max(int a, int b) { return a > b ? a : b; }
static inline int d3d_min(int a, int b) { return a < b ? a : b; }

static inline uint32_t d3d_blend_pixel(uint32_t dst, uint32_t src) {
    uint32_t a = (src >> 24) & 0xFF;
    if (a == 0) return dst;
    if (a == 255) return src | 0xFF000000u;
    uint32_t inv_a = 255 - a;

    uint32_t rb_s = src & 0x00FF00FFu;
    uint32_t g_s  = src & 0x0000FF00u;
    uint32_t rb_d = dst & 0x00FF00FFu;
    uint32_t g_d  = dst & 0x0000FF00u;

    uint32_t rb = ((rb_s * a + rb_d * inv_a) >> 8) & 0x00FF00FFu;
    uint32_t g  = ((g_s * a + g_d * inv_a) >> 8) & 0x0000FF00u;

    uint32_t da = (dst >> 24) & 0xFF;
    uint32_t out_a = a + ((da * inv_a) >> 8);
    if (out_a > 255) out_a = 255;

    return (out_a << 24) | rb | g;
}

void d3d_init(uint32_t screen_w, uint32_t screen_h) {
    g_d3d_device.render_target = NULL;
    g_d3d_device.viewport_w    = screen_w;
    g_d3d_device.viewport_h    = screen_h;
    g_d3d_device.blend_mode    = 1;
    g_d3d_device.global_alpha  = 255;
}

void d3d_set_render_target(d3d_texture_t *target) {
    g_d3d_device.render_target = target;
}

void d3d_clear(d3d_texture_t *tex, uint32_t color) {
    if (!tex || !tex->pixels) return;
    uint32_t count = tex->stride * tex->height;
    for (uint32_t i = 0; i < count; i++) {
        tex->pixels[i] = color;
    }
}

void d3d_fill_rect(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t color) {
    if (!tex || !tex->pixels || w <= 0 || h <= 0) return;
    int x0 = d3d_max(x, 0);
    int y0 = d3d_max(y, 0);
    int x1 = d3d_min(x + w, (int)tex->width);
    int y1 = d3d_min(y + h, (int)tex->height);
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t a = (color >> 24) & 0xFF;
    for (int cy = y0; cy < y1; cy++) {
        uint32_t *row = tex->pixels + cy * tex->stride;
        if (a == 255) {
            for (int cx = x0; cx < x1; cx++) row[cx] = color;
        } else if (a > 0) {
            for (int cx = x0; cx < x1; cx++) row[cx] = d3d_blend_pixel(row[cx], color);
        }
    }
}

void d3d_draw_rect(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t color, int thickness) {
    if (!tex || !tex->pixels || w <= 0 || h <= 0 || thickness <= 0) return;
    d3d_fill_rect(tex, x, y, w, thickness, color);
    d3d_fill_rect(tex, x, y + h - thickness, w, thickness, color);
    d3d_fill_rect(tex, x, y + thickness, thickness, h - 2 * thickness, color);
    d3d_fill_rect(tex, x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}

void d3d_gradient_v(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t c_top, uint32_t c_bot) {
    if (!tex || !tex->pixels || w <= 0 || h <= 0) return;
    int x0 = d3d_max(x, 0);
    int y0 = d3d_max(y, 0);
    int x1 = d3d_min(x + w, (int)tex->width);
    int y1 = d3d_min(y + h, (int)tex->height);
    if (x0 >= x1 || y0 >= y1) return;

    int r0 = (c_top >> 16) & 0xFF, g0 = (c_top >> 8) & 0xFF, b0 = c_top & 0xFF, a0 = (c_top >> 24) & 0xFF;
    int r1 = (c_bot >> 16) & 0xFF, g1 = (c_bot >> 8) & 0xFF, b1 = c_bot & 0xFF, a1 = (c_bot >> 24) & 0xFF;

    for (int cy = y0; cy < y1; cy++) {
        int t = (cy - y) * 255 / (h > 1 ? h - 1 : 1);
        uint32_t r = (uint32_t)(r0 + ((r1 - r0) * t) / 255);
        uint32_t g = (uint32_t)(g0 + ((g1 - g0) * t) / 255);
        uint32_t b = (uint32_t)(b0 + ((b1 - b0) * t) / 255);
        uint32_t a = (uint32_t)(a0 + ((a1 - a0) * t) / 255);
        uint32_t col = (a << 24) | (r << 16) | (g << 8) | b;

        uint32_t *row = tex->pixels + cy * tex->stride;
        for (int cx = x0; cx < x1; cx++) {
            row[cx] = (a == 255) ? col : d3d_blend_pixel(row[cx], col);
        }
    }
}

void d3d_gradient_h(d3d_texture_t *tex, int x, int y, int w, int h, uint32_t c_left, uint32_t c_right) {
    if (!tex || !tex->pixels || w <= 0 || h <= 0) return;
    int x0 = d3d_max(x, 0);
    int y0 = d3d_max(y, 0);
    int x1 = d3d_min(x + w, (int)tex->width);
    int y1 = d3d_min(y + h, (int)tex->height);
    if (x0 >= x1 || y0 >= y1) return;

    int r0 = (c_left >> 16) & 0xFF, g0 = (c_left >> 8) & 0xFF, b0 = c_left & 0xFF, a0 = (c_left >> 24) & 0xFF;
    int r1 = (c_right >> 16) & 0xFF, g1 = (c_right >> 8) & 0xFF, b1 = c_right & 0xFF, a1 = (c_right >> 24) & 0xFF;

    for (int cx = x0; cx < x1; cx++) {
        int t = (cx - x) * 255 / (w > 1 ? w - 1 : 1);
        uint32_t r = (uint32_t)(r0 + ((r1 - r0) * t) / 255);
        uint32_t g = (uint32_t)(g0 + ((g1 - g0) * t) / 255);
        uint32_t b = (uint32_t)(b0 + ((b1 - b0) * t) / 255);
        uint32_t a = (uint32_t)(a0 + ((a1 - a0) * t) / 255);
        uint32_t col = (a << 24) | (r << 16) | (g << 8) | b;

        for (int cy = y0; cy < y1; cy++) {
            uint32_t *p = tex->pixels + cy * tex->stride + cx;
            *p = (a == 255) ? col : d3d_blend_pixel(*p, col);
        }
    }
}

void d3d_blit(d3d_texture_t *dst, int dx, int dy, const d3d_texture_t *src, int sx, int sy, int w, int h) {
    if (!dst || !src || !dst->pixels || !src->pixels || w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        int d_y = dy + y;
        int s_y = sy + y;
        if (d_y < 0 || d_y >= (int)dst->height || s_y < 0 || s_y >= (int)src->height) continue;
        for (int x = 0; x < w; x++) {
            int d_x = dx + x;
            int s_x = sx + x;
            if (d_x < 0 || d_x >= (int)dst->width || s_x < 0 || s_x >= (int)src->width) continue;
            uint32_t sp = src->pixels[s_y * src->stride + s_x];
            uint32_t *dp = dst->pixels + d_y * dst->stride + d_x;
            *dp = d3d_blend_pixel(*dp, sp);
        }
    }
}

void d3d_blend_copy(uint32_t *dst, const uint32_t *src, uint32_t count, uint8_t alpha) {
    if (!dst || !src || count == 0) return;
    if (alpha == 255) {
        for (uint32_t i = 0; i < count; i++) dst[i] = d3d_blend_pixel(dst[i], src[i]);
    } else if (alpha > 0) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t s = src[i];
            uint32_t sa = (((s >> 24) & 0xFF) * alpha) / 255;
            uint32_t sp = (sa << 24) | (s & 0x00FFFFFFu);
            dst[i] = d3d_blend_pixel(dst[i], sp);
        }
    }
}

void d3d_acrylic_glass(d3d_texture_t *dst, const d3d_texture_t *backdrop,
                       int x, int y, int w, int h, uint32_t tint, int radius) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0) return;
    if (!backdrop || !backdrop->pixels) {
        d3d_fill_rect(dst, x, y, w, h, tint | 0xFF000000u);
        return;
    }
    if (radius < 1) radius = 1;
    if (radius > 4) radius = 4;

    uint32_t tr = (tint >> 16) & 0xFF;
    uint32_t tg = (tint >> 8) & 0xFF;
    uint32_t tb = tint & 0xFF;
    uint32_t ta = (tint >> 24) & 0xFF;

    for (int py = 0; py < h; py++) {
        int ey = y + py;
        if (ey < 0 || ey >= (int)dst->height) continue;
        for (int px = 0; px < w; px++) {
            int ex = x + px;
            if (ex < 0 || ex >= (int)dst->width) continue;

            uint32_t rr = 0, rg = 0, rb = 0, cnt = 0;
            for (int dy = -radius; dy <= radius; dy++) {
                int by = ey + dy;
                if (by < 0) by = 0;
                if (by >= (int)backdrop->height) by = (int)backdrop->height - 1;
                for (int dx = -radius; dx <= radius; dx++) {
                    int bx = ex + dx;
                    if (bx < 0) bx = 0;
                    if (bx >= (int)backdrop->width) bx = (int)backdrop->width - 1;
                    uint32_t bp = backdrop->pixels[by * backdrop->stride + bx];
                    rr += (bp >> 16) & 0xFF;
                    rg += (bp >> 8) & 0xFF;
                    rb += bp & 0xFF;
                    cnt++;
                }
            }
            if (cnt) { rr /= cnt; rg /= cnt; rb /= cnt; }
            if (ta > 0) {
                rr = (rr * (255 - ta) + tr * ta) / 255;
                rg = (rg * (255 - ta) + tg * ta) / 255;
                rb = (rb * (255 - ta) + tb * ta) / 255;
            }
            dst->pixels[ey * dst->stride + ex] = (0xFFu << 24) | (rr << 16) | (rg << 8) | rb;
        }
    }
}

void d3d_drop_shadow(d3d_texture_t *dst, int x, int y, int w, int h, int radius, uint8_t max_alpha) {
    if (!dst || !dst->pixels || w <= 0 || h <= 0 || radius <= 0) return;
    for (int r = 0; r < radius; r++) {
        uint32_t a = (uint32_t)((radius - r) * max_alpha / radius);
        uint32_t col = (a << 24);
        int sx = x - r, sy = y - r, sw = w + 2 * r, sh = h + 2 * r;
        d3d_draw_rect(dst, sx, sy, sw, sh, col, 1);
    }
}

void d3d_rounded_rect(d3d_texture_t *tex, int x, int y, int w, int h, int radius, uint32_t color) {
    if (!tex || !tex->pixels || w <= 0 || h <= 0) return;
    if (radius <= 0) {
        d3d_fill_rect(tex, x, y, w, h, color);
        return;
    }
    int r2 = radius * radius;
    for (int cy = 0; cy < h; cy++) {
        int py = y + cy;
        if (py < 0 || py >= (int)tex->height) continue;
        for (int cx = 0; cx < w; cx++) {
            int px = x + cx;
            if (px < 0 || px >= (int)tex->width) continue;

            int dx = 0, dy = 0;
            if (cx < radius) dx = radius - cx;
            else if (cx >= w - radius) dx = cx - (w - radius - 1);

            if (cy < radius) dy = radius - cy;
            else if (cy >= h - radius) dy = cy - (h - radius - 1);

            if (dx * dx + dy * dy <= r2) {
                uint32_t *p = tex->pixels + py * tex->stride + px;
                *p = d3d_blend_pixel(*p, color);
            }
        }
    }
}
