/*
 * SHENLONG OS - DWM (Desktop Window Manager)
 * Retained pencere birleştirici, Fluent efektleri ve dinamik pencere boyutu adaptasyonu.
 */
#ifndef SHENLONG_DWM_H
#define SHENLONG_DWM_H

#include "kernel.h"
#include "gpu.h"
#include "d3d.h"

typedef struct {
    int      x, y, w, h;
    int      visible;
    int      active;
    int      maximized;
    int      minimized;
    char     title[64];
    gpu_surface_t *surface;
    gpu_surface_t *shadow;
} dwm_window_t;

/* DWM API */
void dwm_init(void *fb_addr, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp);
void dwm_compose(void);
void dwm_damage(int x, int y, int w, int h);
void dwm_draw_window_frame(gpu_surface_t *s, int w, int h, const char *title, int active, int hovered_btn);
void dwm_draw_drop_shadow(gpu_surface_t *shadow, int w, int h);

/* Parallels ve pencere boyutu adaptasyonu */
void dwm_resize_display(uint32_t new_w, uint32_t new_h);

#endif /* SHENLONG_DWM_H */
