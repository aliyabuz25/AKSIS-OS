/*
 * SHENLONG OS - Wallpaper Sistemi
 * Disk imajından wallpaper yükler ve GPU surface'e çizer
 */
#ifndef WALLPAPER_H
#define WALLPAPER_H

#include "kernel.h"
#include "gpu.h"

#define WALLPAPER_MAX 16

typedef struct {
    char     name[64];
    uint32_t w, h;
    uint32_t data_sector;
    uint32_t data_size;
} wallpaper_entry_t;

/* Wallpaper disk imajını tara, entry listesini doldur */
int  wallpaper_init(void);

/* Wallpaper sayısı */
int  wallpaper_count(void);

/* i. wallpaper'ı GPU surface'e yükle (oranı koruyarak) */
int  wallpaper_load(int index, gpu_surface_t *dst);

/* Aktif wallpaper index'i al/set et */
int  wallpaper_get_active(void);
void wallpaper_set_active(int index);

/* Entry bilgisi */
const wallpaper_entry_t *wallpaper_get_entry(int index);

/* Slideshow: her 10 saniyede bir fade-in ile geçiş.
   wallpaper_tick() ui_poll() içinden çağrılır.
   wallpaper_is_fading() != 0 ise wallpaper_draw_blended() ile çiz. */
void wallpaper_tick(uint64_t now_ms, gpu_surface_t *wall_surf);
int  wallpaper_is_fading(void);

/* İki surface arasında anlık blend (fade_alpha 0=eski, 255=yeni) */
void wallpaper_blit_blended(gpu_surface_t *dst, uint32_t fade_alpha);

#endif