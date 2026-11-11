/*
 * SHENLONG OS - GPU Compositor
 * Double buffer, alpha compositing, z-order surface sistemi
 */

#include "gpu.h"
#include "kernel.h"
#include "kernel_info.h"
#include "logo.h"
#include "mouse.h"

/* ------------------------------------------------------------------ */
/* Framebuffer globals                                                 */
/* ------------------------------------------------------------------ */
uint8_t  *gpu_fb       = NULL;
uint32_t  gpu_fb_w     = 0;
uint32_t  gpu_fb_h     = 0;
uint32_t  gpu_fb_pitch = 0;
uint32_t  gpu_fb_bpx   = 0;

/* Back buffer — heap'te allocate edilir (mem_init sonrası) */
static uint32_t *back_buf = NULL;

/* ------------------------------------------------------------------ */
/* Surface pool — heap yerine statik pool                             */
/* ------------------------------------------------------------------ */
/* Her surface max 1920x1080 olabilir ama bunları statik tutamayız.
   Bunun yerine her surface kendi pixel bölgesini back_buf'tan AYIRMAZ,
   surface'ler kendi malloc'd piksel bufferlarını taşır.
   Freestanding ortamda kmalloc kullanıyoruz. */

static gpu_surface_t surfaces[GPU_MAX_SURFACES];

/* ------------------------------------------------------------------ */
/* gpu_init                                                           */
/* ------------------------------------------------------------------ */
void gpu_init(void *fb, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp) {
    gpu_fb       = (uint8_t *)fb;
    gpu_fb_w     = w;
    gpu_fb_h     = h;
    gpu_fb_pitch = pitch;
    gpu_fb_bpx   = bpp / 8;

    for (int i = 0; i < GPU_MAX_SURFACES; i++)
        surfaces[i].used = 0;

    /* Back buffer heap'ten allocate et */
    uint32_t total = w * h;
    back_buf = (uint32_t *)kmalloc(total * 4);
    if (!back_buf) return; /* heap yoksa direkt fb'ye yaz */
    for (uint32_t i = 0; i < total; i++) back_buf[i] = 0xFF000000u;
}

/* ------------------------------------------------------------------ */
/* Surface yönetimi                                                   */
/* ------------------------------------------------------------------ */
gpu_surface_t *gpu_surface_create(uint32_t w, uint32_t h, int32_t x, int32_t y, int z) {
    gpu_surface_t *s = NULL;
    for (int i = 0; i < GPU_MAX_SURFACES; i++) {
        if (!surfaces[i].used) { s = &surfaces[i]; break; }
    }
    if (!s) return NULL;

    uint32_t npix = w * h;
    s->pixels  = (uint32_t *)kmalloc(npix * 4);
    if (!s->pixels) return NULL;

    s->w       = w;
    s->h       = h;
    s->x       = x;
    s->y       = y;
    s->stride  = w;
    s->visible = 1;
    s->alpha   = 255;
    s->z       = z;
    s->used    = 1;

    /* Temizle */
    for (uint32_t i = 0; i < npix; i++) s->pixels[i] = 0xFF000000u;
    return s;
}

void gpu_surface_destroy(gpu_surface_t *s) {
    if (!s || !s->used) return;
    kfree(s->pixels);
    s->pixels = NULL;
    s->used   = 0;
}

void gpu_surface_move(gpu_surface_t *s, int32_t x, int32_t y) {
    if (!s) return;
    s->x = x; s->y = y;
}

void gpu_surface_show(gpu_surface_t *s, int visible) {
    if (s) s->visible = visible;
}

/* ------------------------------------------------------------------ */
/* Surface çizim primitifleri                                         */
/* ------------------------------------------------------------------ */
void gpu_fill(gpu_surface_t *s, uint32_t color) {
    if (!s) return;
    uint32_t n = s->w * s->h;
    uint32_t c = color | 0xFF000000u;
    for (uint32_t i = 0; i < n; i++) s->pixels[i] = c;
}

void gpu_fill_rect(gpu_surface_t *s, int32_t x, int32_t y,
                   uint32_t w, uint32_t h, uint32_t color) {
    if (!s) return;
    int32_t x1 = x + (int32_t)w;
    int32_t y1 = y + (int32_t)h;
    if (x1 > (int32_t)s->w) x1 = (int32_t)s->w;
    if (y1 > (int32_t)s->h) y1 = (int32_t)s->h;
    for (int32_t py = y; py < y1; py++)
        for (int32_t px = x; px < x1; px++)
            gpu_put_pixel(s, px, py, color);
}

void gpu_gradient_v(gpu_surface_t *s, int32_t x, int32_t y,
                    uint32_t w, uint32_t h, uint32_t top, uint32_t bot) {
    if (!s || h == 0) return;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t t = dy * 255 / (h - 1 > 0 ? h - 1 : 1);
        uint32_t r = (GPU_R(top)*(255-t) + GPU_R(bot)*t) / 255;
        uint32_t g = (GPU_G(top)*(255-t) + GPU_G(bot)*t) / 255;
        uint32_t b = (GPU_B(top)*(255-t) + GPU_B(bot)*t) / 255;
        uint32_t color = GPU_RGB(r, g, b);
        for (uint32_t dx = 0; dx < w; dx++)
            gpu_put_pixel(s, x + (int32_t)dx, y + (int32_t)dy, color);
    }
}

void gpu_draw_rect(gpu_surface_t *s, int32_t x, int32_t y,
                   uint32_t w, uint32_t h, uint32_t color, int thick) {
    if (!s) return;
    for (int t = 0; t < thick; t++) {
        for (uint32_t i = 0; i < w; i++) {
            gpu_put_pixel(s, x+(int32_t)i,         y+t,              color);
            gpu_put_pixel(s, x+(int32_t)i,         y+(int32_t)h-1-t, color);
        }
        for (uint32_t i = 0; i < h; i++) {
            gpu_put_pixel(s, x+t,              y+(int32_t)i,         color);
            gpu_put_pixel(s, x+(int32_t)w-1-t, y+(int32_t)i,         color);
        }
    }
}

void gpu_draw_line(gpu_surface_t *s, int32_t x0, int32_t y0,
                   int32_t x1, int32_t y1, uint32_t color) {
    int32_t dx = x1>x0 ? x1-x0 : x0-x1;
    int32_t dy = y1>y0 ? y1-y0 : y0-y1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        gpu_put_pixel(s, x0, y0, color);
        if (x0==x1 && y0==y1) break;
        int32_t e2 = 2*err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void gpu_blit(gpu_surface_t *dst, int32_t dx, int32_t dy,
              const gpu_surface_t *src, int32_t sx, int32_t sy,
              uint32_t w, uint32_t h) {
    if (!dst || !src) return;
    for (uint32_t row = 0; row < h; row++) {
        int32_t sy2 = sy + (int32_t)row;
        int32_t dy2 = dy + (int32_t)row;
        if (sy2 < 0 || (uint32_t)sy2 >= src->h) continue;
        if (dy2 < 0 || (uint32_t)dy2 >= dst->h) continue;
        for (uint32_t col = 0; col < w; col++) {
            int32_t sx2 = sx + (int32_t)col;
            int32_t dx2 = dx + (int32_t)col;
            if (sx2 < 0 || (uint32_t)sx2 >= src->w) continue;
            uint32_t spix = src->pixels[(uint32_t)sy2 * src->stride + (uint32_t)sx2];
            gpu_put_pixel(dst, dx2, dy2, spix);
        }
    }
}

void gpu_blit_logo(gpu_surface_t *dst, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!dst) return;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t sy = (uint32_t)((uint64_t)dy * LOGO_H / h);
        if (sy >= (uint32_t)LOGO_H) sy = LOGO_H - 1;
        for (uint32_t dx = 0; dx < w; dx++) {
            uint32_t sx = (uint32_t)((uint64_t)dx * LOGO_W / w);
            if (sx >= (uint32_t)LOGO_W) sx = LOGO_W - 1;
            uint32_t idx = (sy * (uint32_t)LOGO_W + sx) * 3;
            uint8_t r = logo_rgb[idx];
            uint8_t g = logo_rgb[idx+1];
            uint8_t b = logo_rgb[idx+2];
            gpu_put_pixel(dst, x+(int32_t)dx, y+(int32_t)dy, GPU_RGB(r,g,b));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Compositor — z-order sort (bubble sort, max 16 surface)            */
/* ------------------------------------------------------------------ */
static void compose_surface(const gpu_surface_t *s) {
    int32_t sx0 = s->x, sy0 = s->y;
    uint32_t sw = s->w, sh = s->h;
    uint32_t ga = (uint32_t)s->alpha;

    for (uint32_t row = 0; row < sh; row++) {
        int32_t fy = sy0 + (int32_t)row;
        if (fy < 0 || (uint32_t)fy >= gpu_fb_h) continue;
        const uint32_t *src_row = s->pixels + row * s->stride;
        uint32_t       *dst_row = back_buf  + (uint32_t)fy * gpu_fb_w;

        for (uint32_t col = 0; col < sw; col++) {
            int32_t fx = sx0 + (int32_t)col;
            if (fx < 0 || (uint32_t)fx >= gpu_fb_w) continue;

            uint32_t spix = src_row[col];
            uint32_t a    = GPU_A(spix);
            if (ga < 255) a = a * ga / 255;
            if (a == 0) continue;

            uint32_t dpix = dst_row[(uint32_t)fx];
            if (a == 255) {
                dst_row[(uint32_t)fx] = spix | 0xFF000000u;
            } else {
                dst_row[(uint32_t)fx] = gpu_alpha_blend(dpix, (spix & 0x00FFFFFFu) | (a << 24));
            }
        }
    }
}

/* Pre-baked wallpaper — wallpaper surface z=0 ise buraya snapshot alinir */
static uint32_t *wallpaper_snap = NULL;
static int       wallpaper_snap_valid = 0;

void gpu_wallpaper_bake(void) {
    /* z=0 surface'i wallpaper_snap'e kopyala */
    for (int i = 0; i < GPU_MAX_SURFACES; i++) {
        if (surfaces[i].used && surfaces[i].visible && surfaces[i].z == 0) {
            if (!wallpaper_snap)
                wallpaper_snap = (uint32_t*)kmalloc(gpu_fb_w * gpu_fb_h * 4);
            if (!wallpaper_snap) return;
            uint32_t n = gpu_fb_w * gpu_fb_h;
            uint32_t *src = surfaces[i].pixels;
            for (uint32_t j = 0; j < n; j++) wallpaper_snap[j] = src[j] | 0xFF000000u;
            wallpaper_snap_valid = 1;
            return;
        }
    }
    wallpaper_snap_valid = 0;
}

/* compose_surface opaque fast-path: tamamen opak surface icin row memcpy */
static void compose_surface_fast(const gpu_surface_t *s) {
    int32_t sx0 = s->x, sy0 = s->y;
    uint32_t sw = s->w, sh = s->h;
    uint32_t ga = (uint32_t)s->alpha;

    /* Tamamen opak ve full-alpha: sadece src row'unu back_buf'a kopyala */
    if (ga == 255) {
        for (uint32_t row = 0; row < sh; row++) {
            int32_t fy = sy0 + (int32_t)row;
            if (fy < 0 || (uint32_t)fy >= gpu_fb_h) continue;
            const uint32_t *src_row = s->pixels + row * s->stride;
            uint32_t       *dst_row = back_buf + (uint32_t)fy * gpu_fb_w;
            int32_t x0 = sx0 < 0 ? 0 : sx0;
            int32_t x1 = sx0 + (int32_t)sw;
            if (x1 > (int32_t)gpu_fb_w) x1 = (int32_t)gpu_fb_w;
            if (x0 >= x1) continue;
            uint32_t off = (uint32_t)(x0 - sx0);
            uint32_t len = (uint32_t)(x1 - x0);
            /* 64-bit fast copy */
            uint32_t *d = dst_row + x0;
            const uint32_t *src = src_row + off;
            uint32_t k = 0;
            for (; k + 2 <= len; k += 2)
                *(uint64_t*)(d+k) = *(const uint64_t*)(src+k);
            if (k < len) d[k] = src[k] | 0xFF000000u;
        }
        return;
    }
    /* Alpha blend fallback */
    compose_surface(s);
}

static inline uint64_t gpu_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* TSC frame limiter: max 30fps = 33ms arasi */
static uint64_t last_compose_tsc = 0;
#define COMPOSE_MIN_MS 10ULL  /* ~100fps */

void gpu_compose(void) {
    if (!back_buf || !gpu_fb) return;

    /* TSC ile throttle — 30fps'ten hizli compose yapma */
    if (gpu_fb_w > 0 && last_compose_tsc > 0 && g_sysinfo.tsc_freq_hz > 0) {
        uint64_t min_ticks = g_sysinfo.tsc_freq_hz / 1000ULL * COMPOSE_MIN_MS;
        if (gpu_rdtsc() - last_compose_tsc < min_ticks) return;
    }
    last_compose_tsc = gpu_rdtsc();
    cursor_invalidate();

    /* Back buffer'i wallpaper snapshot ile baslat (sifirdan temizlemek yerine) */
    uint32_t n = gpu_fb_w * gpu_fb_h;
    if (wallpaper_snap_valid && wallpaper_snap) {
        uint32_t k = 0;
        for (; k + 2 <= n; k += 2)
            *(uint64_t*)(back_buf+k) = *(uint64_t*)(wallpaper_snap+k);
        if (k < n) back_buf[k] = wallpaper_snap[k];
    } else {
        uint32_t bg = GPU_COL_DESKTOP | 0xFF000000u;
        for (uint32_t i = 0; i < n; i++) back_buf[i] = bg;
    }

    /* Z-order sırala */
    int order[GPU_MAX_SURFACES];
    int cnt = 0;
    for (int i = 0; i < GPU_MAX_SURFACES; i++) {
        if (surfaces[i].used && surfaces[i].visible && surfaces[i].z > 0)
            order[cnt++] = i;
    }
    /* Insertion sort by z */
    for (int i = 1; i < cnt; i++) {
        int key = order[i], j = i-1;
        while (j >= 0 && surfaces[order[j]].z > surfaces[key].z) {
            order[j+1] = order[j]; j--;
        }
        order[j+1] = key;
    }

    for (int i = 0; i < cnt; i++)
        compose_surface_fast(&surfaces[order[i]]);

    /* Back buffer → framebuffer: 64-bit aligned write */
    if (gpu_fb_bpx == 4) {
        for (uint32_t y = 0; y < gpu_fb_h; y++) {
            const uint32_t *src = back_buf + y * gpu_fb_w;
            uint32_t *dst = (uint32_t*)(gpu_fb + (size_t)y * gpu_fb_pitch);
            uint32_t x = 0;
            for (; x + 4 <= gpu_fb_w; x += 4) {
                *(uint64_t*)(dst+x)   = *(const uint64_t*)(src+x);
                *(uint64_t*)(dst+x+2) = *(const uint64_t*)(src+x+2);
            }
            for (; x < gpu_fb_w; x++) dst[x] = src[x];
        }
    } else {
        for (uint32_t y = 0; y < gpu_fb_h; y++) {
            uint32_t *src = back_buf + y * gpu_fb_w;
            for (uint32_t x = 0; x < gpu_fb_w; x++) {
                uint8_t *p = gpu_fb + (size_t)y * gpu_fb_pitch + x * gpu_fb_bpx;
                p[0] = GPU_B(src[x]); p[1] = GPU_G(src[x]); p[2] = GPU_R(src[x]);
            }
        }
    }
    cursor_draw();
}
