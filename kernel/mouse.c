/*
 * SHENLONG OS - PS/2 Mouse + GPU Surface Cursor
 * Cursor GPU compositor'de z=999 surface olarak ciziyor
 * Cursor movement invalidates only its previous and current bounds.
 */

#include "mouse.h"
#include "gpu.h"
#include "fb.h"
#include "ps2.h"
#include "kernel.h"
#include "cursor_data.h"

#define CUR_W CURSOR_W
#define CUR_H CURSOR_H

static gpu_surface_t *cursor_surf = NULL;
static int mouse_ok = 0;

mouse_state_t g_mouse;
static uint8_t pkt[3];
static int pkt_i = 0;

/* ------------------------------------------------------------------ */
/* Cursor surface yarat ve beyaz modern imleç dokusunu yükle         */
/* ------------------------------------------------------------------ */
static void cursor_surface_init(void) {
    cursor_surf = gpu_surface_create(CUR_W, CUR_H, 0, 0, 999);
    if (!cursor_surf) return;
    cursor_surf->opaque = 0;
    cursor_surf->visible = 0;

    for(int y=0;y<CUR_H;y++) {
        for(int x=0;x<CUR_W;x++) {
            cursor_surf->pixels[y*CUR_W+x] = g_white_cursor[y][x];
        }
    }
}

/* 
 * Doğal ve Akıcı Fare Balistiği:
 * Mikro hareketlerde (<= 2px) 1:1 piksel hassasiyeti korunur (butonlar, ince tıklamalar).
 * Hızlı el hareketlerinde ivmelenme devreye girerek geniş ekranda zahmetsiz ve akıcı kayma sağlar.
 */
static void apply_mouse_ballistics(int32_t *p_dx, int32_t *p_dy) {
    int32_t dx = *p_dx;
    int32_t dy = *p_dy;
    if (dx == 0 && dy == 0) return;

    /* Donanım sınırları ve aşırı sıçramaları önle */
    if (dx > 127) dx = 127;
    else if (dx < -127) dx = -127;
    if (dy > 127) dy = 127;
    else if (dy < -127) dy = -127;

    uint32_t dist_sq = (uint32_t)(dx * dx + dy * dy);
    int32_t scale_256 = 256; /* 1.0x (8-bit sabit nokta) */

    if (dist_sq <= 4) {
        scale_256 = 256;      /* 1.0x hassas piksel hedefleme */
    } else if (dist_sq <= 25) {
        scale_256 = 358;      /* ~1.4x doğal yumuşak ivmelenme */
    } else if (dist_sq <= 81) {
        scale_256 = 460;      /* ~1.8x orta hızda çevik hareket */
    } else if (dist_sq <= 256) {
        scale_256 = 563;      /* ~2.2x hızlı kaydırma */
    } else {
        scale_256 = 665;      /* ~2.6x geniş ekran süpürme */
    }

    /* Donanım bölme hatası (#DE) üretmeyen güvenli ve ultra hızlı bit kaydırma */
    *p_dx = (dx * scale_256) >> 8;
    *p_dy = (dy * scale_256) >> 8;
}

/* ------------------------------------------------------------------ */
/* mouse_handle_byte                                                  */
/* ------------------------------------------------------------------ */
void mouse_handle_byte(uint8_t data) {
    if (pkt_i == 0 && !(data & 0x08)) return;
    pkt[pkt_i++] = data;
    if (pkt_i < 3) return;
    pkt_i = 0;
    if (pkt[0] & 0xC0) return;
    mouse_ok = 1;
    if (cursor_surf && !cursor_surf->visible) cursor_surf->visible = 1;

    uint8_t pressed = (pkt[0] & 7) & ~g_mouse.buttons;
    g_mouse.buttons = pkt[0] & 0x07;

    int32_t dx = (int32_t)pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    int32_t dy = (int32_t)pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
    dy = -dy;

    apply_mouse_ballistics(&dx, &dy);

    g_mouse.dx = dx; g_mouse.dy = dy;
    g_mouse.x += dx; g_mouse.y += dy;

    if (g_mouse.x < 0)                     g_mouse.x = 0;
    if (g_mouse.y < 0)                      g_mouse.y = 0;
    if (g_mouse.x >= (int32_t)gpu_fb_w-1)  g_mouse.x = (int32_t)gpu_fb_w-1;
    if (g_mouse.y >= (int32_t)gpu_fb_h-1)  g_mouse.y = (int32_t)gpu_fb_h-1;

    g_mouse.moved = (dx || dy) ? 1 : 0;
    if (pressed) {
        g_mouse.pressed |= pressed;
        g_mouse.press_x  = g_mouse.x;
        g_mouse.press_y  = g_mouse.y;
    }

    /* Cursor surface pozisyonunu guncelle */
    if (cursor_surf) {
        cursor_surf->x = g_mouse.x;
        cursor_surf->y = g_mouse.y;
    }
}

/* ------------------------------------------------------------------ */
/* mouse_init                                                         */
/* ------------------------------------------------------------------ */
void mouse_init(void) {
    kmemset(&g_mouse, 0, sizeof(g_mouse));
    g_mouse.x = (int32_t)(gpu_fb_w / 2);
    g_mouse.y = (int32_t)(gpu_fb_h / 2);
    pkt_i = 0;
    mouse_ok = 1;

    cursor_surface_init();

    if (ps2_init()) {
        /* Reset defaults, 1:1 scaling ve veri akışını aç */
        ps2_device_command(1, 0xF6);
        ps2_device_command(1, 0xE6);
        ps2_device_command(1, 0xF4);
    }

    if (cursor_surf) {
        cursor_surf->x = g_mouse.x;
        cursor_surf->y = g_mouse.y;
        cursor_surf->visible = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Eski API compat — artık GPU surface ile calisiyor                  */
/* ------------------------------------------------------------------ */
void cursor_erase(void)    { /* GPU compositor halleder */ }
void cursor_invalidate(void) { /* gerek yok */ }
void cursor_draw(void)     { /* GPU compositor halleder */ }
void cursor_update(void)   { ps2_poll(); }
void mouse_poll(void)      { ps2_poll(); }
int  mouse_available(void) { return mouse_ok; }
void mouse_set_visible(int visible) { if(cursor_surf) cursor_surf->visible=visible; }
