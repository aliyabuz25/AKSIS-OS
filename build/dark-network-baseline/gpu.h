/*
 * SHENLONG OS - GPU Compositor
 * Retained surfaces, damage tracking and paced software presentation.
 */
#ifndef GPU_H
#define GPU_H

#include "kernel.h"

/* ------------------------------------------------------------------ */
/* Renk yardımcıları                                                  */
/* ------------------------------------------------------------------ */
#define GPU_RGBA(r,g,b,a) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define GPU_RGB(r,g,b)    GPU_RGBA(r,g,b,0xFF)
#define GPU_R(c)   (((c)>>16)&0xFF)
#define GPU_G(c)   (((c)>> 8)&0xFF)
#define GPU_B(c)   (((c)    )&0xFF)
#define GPU_A(c)   (((c)>>24)&0xFF)

/* Tema renkleri */
#define GPU_COL_DESKTOP     GPU_RGB(0x08,0x08,0x10)  /* koyu lacivert-siyah */
#define GPU_COL_WIN_TITLE   GPU_RGB(0x1a,0x1a,0x2e)  /* koyu mor */
#define GPU_COL_WIN_BODY    GPU_RGB(0x0d,0x0d,0x1a)  /* çok koyu mor-siyah */
#define GPU_COL_WIN_BORDER  GPU_RGB(0x33,0x33,0x66)  /* mor kenar */
#define GPU_COL_WIN_FOCUS   GPU_RGB(0x44,0x22,0x88)  /* aktif pencere mor */
#define GPU_COL_ACCENT      GPU_RGB(0x88,0x44,0xFF)  /* mor vurgu */
#define GPU_COL_TEXT        GPU_RGB(0xE8,0xE8,0xE8)  /* açık gri yazı */
#define GPU_COL_TEXT_DIM    GPU_RGB(0x88,0x88,0xAA)  /* soluk yazı */
#define GPU_COL_SHADOW      GPU_RGBA(0,0,0,0xA0)      /* yarı şeffaf gölge */
#define GPU_COL_TASKBAR     GPU_RGB(0x10,0x10,0x20)  /* görev çubuğu */
#define GPU_COL_TRANSPARENT GPU_RGBA(0,0,0,0)

/* ------------------------------------------------------------------ */
/* Surface — bağımsız piksel tamponu                                  */
/* ------------------------------------------------------------------ */
#define GPU_MAX_SURFACES 16
#define GPU_SURFACE_BUF  (1920*1080*4)  /* max surface boyutu */

typedef struct {
    uint32_t *pixels;   /* RGBA piksel tamponu */
    uint32_t  w, h;     /* boyutlar */
    int32_t   x, y;     /* ekrandaki konum */
    uint32_t  stride;   /* satır genişliği (piksel cinsinden) */
    int       visible;
    int       alpha;    /* global alpha 0-255 */
    int       z;        /* z-order (büyük = öne) */
    int       used;
    int       opaque;          /* all pixels are opaque (not global alpha) */
    int       opaque_inset;    /* opaque interior of rounded panels, 0=unknown */
} gpu_surface_t;

/* ------------------------------------------------------------------ */
/* GPU API                                                            */
/* ------------------------------------------------------------------ */
void gpu_init(void *fb, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp);

/* Surface yönetimi */
gpu_surface_t *gpu_surface_create(uint32_t w, uint32_t h, int32_t x, int32_t y, int z);
void           gpu_surface_destroy(gpu_surface_t *s);
void           gpu_surface_move(gpu_surface_t *s, int32_t x, int32_t y);
void           gpu_surface_show(gpu_surface_t *s, int visible);

/* Surface çizim primitifleri */
void gpu_fill(gpu_surface_t *s, uint32_t color);
void gpu_fill_rect(gpu_surface_t *s, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
void gpu_gradient_v(gpu_surface_t *s, int32_t x, int32_t y, uint32_t w, uint32_t h,
                    uint32_t top, uint32_t bot);
void gpu_draw_rect(gpu_surface_t *s, int32_t x, int32_t y, uint32_t w, uint32_t h,
                   uint32_t color, int thick);
void gpu_draw_line(gpu_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   uint32_t color);
void gpu_blit(gpu_surface_t *dst, int32_t dx, int32_t dy,
              const gpu_surface_t *src, int32_t sx, int32_t sy, uint32_t w, uint32_t h);
/* Logo blit (logo_rgb formatı) */
void gpu_blit_logo(gpu_surface_t *dst, int32_t x, int32_t y, uint32_t w, uint32_t h);

/* Compositor: tüm surface'leri framebuffer'a yaz */
void gpu_compose(void);
void gpu_damage(int x, int y, int w, int h);
void gpu_surface_damage(gpu_surface_t *s, int x, int y, int w, int h);
void gpu_surface_alpha(gpu_surface_t *s, int alpha);
typedef struct {
    uint64_t frames, pixels, ticks, max_ticks, max_pixels;
} gpu_stats_t;
void gpu_stats_read(gpu_stats_t *out);
void gpu_stats_reset(void);
/* Wallpaper snapshot al — sadece wallpaper değişince çağır */
void gpu_wallpaper_bake(void);

/* Yardımcı */
static inline uint32_t gpu_alpha_blend(uint32_t dst, uint32_t src) {
    uint32_t a = GPU_A(src);
    if (a == 0)   return dst;
    if (a == 255) return src | 0xFF000000u;
    uint32_t na = 255 - a;
    uint32_t r = (GPU_R(src)*a + GPU_R(dst)*na) / 255;
    uint32_t g = (GPU_G(src)*a + GPU_G(dst)*na) / 255;
    uint32_t b = (GPU_B(src)*a + GPU_B(dst)*na) / 255;
    return GPU_RGBA(r,g,b,0xFF);
}

static inline void gpu_put_pixel(gpu_surface_t *s, int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= s->w || (uint32_t)y >= s->h) return;
    uint32_t a = GPU_A(color);
    if (a == 0) return;
    uint32_t *p = s->pixels + (uint32_t)y * s->stride + (uint32_t)x;
    if (a == 255) { *p = color; return; }
    *p = gpu_alpha_blend(*p, color);
}

/* Framebuffer erişimi (fb.c için) */
extern uint8_t  *gpu_fb;
extern uint32_t  gpu_fb_w, gpu_fb_h, gpu_fb_pitch, gpu_fb_bpx;

#endif /* GPU_H */
