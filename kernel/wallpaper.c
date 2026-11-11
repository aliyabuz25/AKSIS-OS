/*
 * SHENLONG OS - Wallpaper Loader
 * virtio-blk disk imajından wallpaper okur
 */

#include "wallpaper.h"
#include "kernel.h"
#include "gpu.h"

#define SECTOR_SIZE 512
#define MAGIC "SHLWP\x00\x00\x00"

/* ------------------------------------------------------------------ */
/* Disk I/O — virtio-blk veya direkt bellek mapped disk              */
/* ------------------------------------------------------------------ */
/* Wallpaper imajı ISO içine gömülü sabit adres olarak erişilir.
   Limine memory map'ten alınan base + offset ile okuruz.
   Basit yaklaşım: imaj build sırasında kernel ELF'e symbol olarak gömülür */

extern const uint8_t wallpaper_img_data[];  /* linker symbol */
extern const uint32_t wallpaper_img_size;

static wallpaper_entry_t entries[WALLPAPER_MAX];
static int entry_count = 0;
static int active_index = 0;
static int initialized = 0;

/* Slideshow state */
#define SLIDE_INTERVAL_MS  60000   /* 60 saniyede bir sakin geçiş */
#define FADE_DURATION_MS   400     /* 0.4 saniye hızlı ve kesintisiz fade */

static uint64_t last_switch_ms = 0;
static int      fade_active    = 0;
static uint64_t fade_start_ms  = 0;
static int      fade_alpha     = 0;   /* 0=eski, 255=yeni */
static int      next_index     = 0;

/* Çift buffer: surf_a=mevcut, surf_b=hedef — kmalloc ile heap'ten */
#define WALL_PIX_MAX (1920*1080)
static uint32_t *surf_a_pix = NULL;
static uint32_t *surf_b_pix = NULL;
static gpu_surface_t surf_a = {0};
static gpu_surface_t surf_b = {0};

/* ------------------------------------------------------------------ */
/* Sektor okuma                                                       */
/* ------------------------------------------------------------------ */
static const uint8_t *read_sector(uint32_t sector) {
    if (!wallpaper_img_data) return NULL;
    uint32_t off = sector * SECTOR_SIZE;
    if (off >= wallpaper_img_size) return NULL;
    return wallpaper_img_data + off;
}

/* ------------------------------------------------------------------ */
/* wallpaper_init                                                     */
/* ------------------------------------------------------------------ */
int wallpaper_init(void) {
    if (initialized) return entry_count;
    entry_count = 0;

    const uint8_t *hdr = read_sector(0);
    if (!hdr) return 0;

    /* Magic kontrol */
    if (kmemcmp(hdr, (const uint8_t*)MAGIC, 8) != 0) return 0;

    uint32_t count;
    kmemcpy(&count, hdr + 8, 4);
    if (count > WALLPAPER_MAX) count = WALLPAPER_MAX;

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry_sec = read_sector(1 + i);
        if (!entry_sec) break;

        wallpaper_entry_t *e = &entries[entry_count];
        kmemcpy(e->name, entry_sec, 64);
        e->name[63] = '\0';
        kmemcpy(&e->w,           entry_sec + 64,  4);
        kmemcpy(&e->h,           entry_sec + 68,  4);
        kmemcpy(&e->data_sector, entry_sec + 72,  4);
        kmemcpy(&e->data_size,   entry_sec + 76,  4);

        if (e->w > 0 && e->h > 0 && e->w <= 2048 && e->h <= 2048)
            entry_count++;
    }

    initialized = 1;
    return entry_count;
}

int wallpaper_count(void) { return entry_count; }
int wallpaper_get_active(void) { return active_index; }
int wallpaper_is_fading(void) { return fade_active; }

void wallpaper_set_active(int index) {
    if (index >= 0 && index < entry_count)
        active_index = index;
}

/* surf_a/b'yi wallpaper_load için hazırla */
static void surf_init(gpu_surface_t *s, uint32_t *pix, uint32_t w, uint32_t h) {
    s->pixels = pix;
    s->w = w; s->h = h; s->stride = w;
    s->x = 0; s->y = 0;
    s->visible = 1; s->alpha = 255; s->z = 0; s->used = 1;
}

/* Slideshow tick: her frame ui_poll'dan çağrılır */
void wallpaper_tick(uint64_t now_ms, gpu_surface_t *wall_surf) {
    if (entry_count <= 1) return;

    if (!last_switch_ms) last_switch_ms = now_ms;

    /* Fade devam ediyor */
    if (fade_active) {
        uint64_t elapsed = now_ms - fade_start_ms;
        if (elapsed >= (uint64_t)FADE_DURATION_MS) {
            /* Fade bitti: a←b, aktif güncelle */
            fade_active = 0;
            fade_alpha  = 255;
            active_index = next_index;
            last_switch_ms = now_ms;
            /* surf_a = surf_b içeriği */
            for (uint32_t i = 0; i < surf_a.w * surf_a.h; i++)
                surf_a_pix[i] = surf_b_pix[i];
            /* Wall surface'e kopyala */
            uint32_t n = wall_surf->w * wall_surf->h;
            for (uint32_t i = 0; i < n && i < (uint32_t)WALL_PIX_MAX; i++)
                wall_surf->pixels[i] = surf_a_pix[i];
        } else {
            fade_alpha = (int)(elapsed * 255 / FADE_DURATION_MS);
            /* Blend a→b ve wall_surf'e yaz */
            uint32_t n = wall_surf->w * wall_surf->h;
            uint32_t na = (uint32_t)(255 - fade_alpha);
            uint32_t nb = (uint32_t)fade_alpha;
            for (uint32_t i = 0; i < n && i < (uint32_t)WALL_PIX_MAX; i++) {
                uint32_t ca = surf_a_pix[i];
                uint32_t cb = surf_b_pix[i];
                uint32_t r = (((ca>>16)&0xFF)*na + ((cb>>16)&0xFF)*nb) / 255;
                uint32_t g = (((ca>> 8)&0xFF)*na + ((cb>> 8)&0xFF)*nb) / 255;
                uint32_t b = ((( ca    )&0xFF)*na + (( cb    )&0xFF)*nb) / 255;
                wall_surf->pixels[i] = (0xFF000000u|(r<<16)|(g<<8)|b);
            }
        }
        return;
    }

    /* 10 saniye geçti mi? */
    if (now_ms - last_switch_ms < (uint64_t)SLIDE_INTERVAL_MS) return;

    /* Yeni wallpaper seç */
    next_index = (active_index + 1) % entry_count;

    if (!surf_a_pix) surf_a_pix = (uint32_t *)kmalloc(WALL_PIX_MAX * 4);
    if (!surf_b_pix) surf_b_pix = (uint32_t *)kmalloc(WALL_PIX_MAX * 4);
    if (!surf_a_pix || !surf_b_pix) return;

    /* surf_a = mevcut wall_surf içeriği */
    surf_init(&surf_a, surf_a_pix, wall_surf->w, wall_surf->h);
    uint32_t n = wall_surf->w * wall_surf->h;
    for (uint32_t i = 0; i < n && i < (uint32_t)WALL_PIX_MAX; i++)
        surf_a_pix[i] = wall_surf->pixels[i];

    /* surf_b = yeni wallpaper */
    surf_init(&surf_b, surf_b_pix, wall_surf->w, wall_surf->h);
    wallpaper_load(next_index, &surf_b);

    fade_active   = 1;
    fade_alpha    = 0;
    fade_start_ms = now_ms;
}

void wallpaper_blit_blended(gpu_surface_t *dst, uint32_t alpha) {
    (void)dst; (void)alpha; /* tick içinde doğrudan wall_surf'e yazılıyor */
}

const wallpaper_entry_t *wallpaper_get_entry(int index) {
    if (index < 0 || index >= entry_count) return NULL;
    return &entries[index];
}

/* ------------------------------------------------------------------ */
/* wallpaper_load — entry'yi GPU surface'e yükle                     */
/* ------------------------------------------------------------------ */
int wallpaper_load(int index, gpu_surface_t *dst) {
    if (!dst || index < 0 || index >= entry_count) return 0;
    wallpaper_entry_t *e = &entries[index];

    uint32_t src_w = e->w, src_h = e->h;
    uint32_t dst_w = dst->w, dst_h = dst->h;
    if (!src_w || !src_h || !dst_w || !dst_h) return 0;

    uint32_t data_off = e->data_sector * SECTOR_SIZE;
    if (data_off >= wallpaper_img_size) return 0;
    const uint8_t *raw_rgb = wallpaper_img_data + data_off;

    /* Fast path: 1:1 eslesme (or. 1920x1080) */
    if (src_w == dst_w && src_h == dst_h) {
        uint32_t npix = dst_w * dst_h;
        for (uint32_t i = 0; i < npix; i++) {
            uint32_t o = i * 3;
            dst->pixels[i] = GPU_RGBA(raw_rgb[o], raw_rgb[o + 1], raw_rgb[o + 2], 0xFF);
        }
        return 1;
    }

    /* Cover / Tam sigdirma: Ekrani tamamen kapla, siyah bant kalmasin */
    uint32_t draw_w = dst_w;
    uint32_t draw_h = (uint32_t)((uint64_t)draw_w * src_h / src_w);
    if (draw_h < dst_h) {
        draw_h = dst_h;
        draw_w = (uint32_t)((uint64_t)draw_h * src_w / src_h);
    }
    int32_t ox = (int32_t)(dst_w - draw_w) / 2;
    int32_t oy = (int32_t)(dst_h - draw_h) / 2;

    /* Once siyah doldur */
    uint32_t npix = dst_w * dst_h;
    for (uint32_t i = 0; i < npix; i++) dst->pixels[i] = GPU_RGBA(0,0,0,0xFF);

    for (uint32_t dy = 0; dy < draw_h; dy++) {
        uint32_t sy = (uint32_t)((uint64_t)dy * src_h / draw_h);
        if (sy >= src_h) sy = src_h - 1;
        int32_t py = oy + (int32_t)dy;
        if (py < 0 || (uint32_t)py >= dst_h) continue;

        uint32_t *dst_row = dst->pixels + (uint32_t)py * dst->stride;
        const uint8_t *src_row = raw_rgb + sy * src_w * 3;

        for (uint32_t dx = 0; dx < draw_w; dx++) {
            int32_t px = ox + (int32_t)dx;
            if (px < 0 || (uint32_t)px >= dst_w) continue;

            uint32_t sx = (uint32_t)((uint64_t)dx * src_w / draw_w);
            if (sx >= src_w) sx = src_w - 1;

            const uint8_t *p = src_row + sx * 3;
            dst_row[px] = GPU_RGBA(p[0], p[1], p[2], 0xFF);
        }
    }

    return 1;
}