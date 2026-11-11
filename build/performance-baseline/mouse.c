/*
 * SHENLONG OS - PS/2 Mouse + GPU Surface Cursor
 * Cursor GPU compositor'de z=999 surface olarak ciziyor
 * gpu_compose() sonrasi otomatik ustte - hic takılmaz
 */

#include "mouse.h"
#include "gpu.h"
#include "fb.h"
#include "ps2.h"
#include "kernel.h"

/* ------------------------------------------------------------------ */
/* Cursor sprite — 13x20 temiz ok cursor                             */
/* ------------------------------------------------------------------ */
#define CUR_W 13
#define CUR_H 20

/* Her pixel: (alpha<<8)|lum — 0=seffaf, 255=opak */
static const uint8_t cur_alpha[CUR_H][CUR_W] = {
    {255,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {255,255,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {255,255,255,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {255,255,255,255,  0,  0,  0,  0,  0,  0,  0,  0,  0},
    {255,255,255,255,255,  0,  0,  0,  0,  0,  0,  0,  0},
    {255,255,255,255,255,255,  0,  0,  0,  0,  0,  0,  0},
    {255,255,255,255,255,255,255,  0,  0,  0,  0,  0,  0},
    {255,255,255,255,255,255,255,255,  0,  0,  0,  0,  0},
    {255,255,255,255,255,255,255,255,255,  0,  0,  0,  0},
    {255,255,255,255,255,255,255,255,255,255,  0,  0,  0},
    {255,255,255,255,255,255,255,255,255,255,255,  0,  0},
    {255,255,255,255,255,255,255,255,255,255,255,255,  0},
    {255,255,255,255,255,255,255,255,255,255,255,255,255},
    {255,255,255,255,255,255,255,255,255,  0,  0,  0,  0},
    {255,255,255,255,255,255,255,255,  0,  0,  0,  0,  0},
    {255,255,255, 90,255,255,  0,  0,  0,  0,  0,  0,  0},
    {255,255, 90, 90,255,255,255,  0,  0,  0,  0,  0,  0},
    {255, 90,  0,  0, 90,255,255,255,  0,  0,  0,  0,  0},
    { 90,  0,  0,  0,  0, 90,255,255,255,  0,  0,  0,  0},
    {  0,  0,  0,  0,  0,  0, 90,255,  0,  0,  0,  0,  0},
};

/* Renk: beyaz ok, siyah kontur */
/* alpha>128 → beyaz dolgu, alpha<128 → siyah kontur */

/* ------------------------------------------------------------------ */
/* GPU cursor surface                                                 */
/* ------------------------------------------------------------------ */
static gpu_surface_t *cursor_surf = NULL;
static int mouse_ok = 0;

mouse_state_t g_mouse;
static uint8_t pkt[3];
static int pkt_i = 0;

/* ------------------------------------------------------------------ */
/* Cursor surface yarat ve piksel doku yükle                          */
/* ------------------------------------------------------------------ */
static void cursor_surface_init(void) {
    cursor_surf = gpu_surface_create(CUR_W, CUR_H, 0, 0, 999);
    if (!cursor_surf) return;

    for (int y = 0; y < CUR_H; y++) {
        for (int x = 0; x < CUR_W; x++) {
            uint8_t a = cur_alpha[y][x];
            uint32_t pix;
            if (a == 0) {
                pix = GPU_RGBA(0,0,0,0); /* seffaf */
            } else if (a > 128) {
                /* Beyaz dolgu, hafif golge */
                uint8_t v = 255;
                pix = GPU_RGBA(v,v,v,a);
            } else {
                /* Siyah kontur */
                pix = GPU_RGBA(0,0,0,200);
            }
            cursor_surf->pixels[y * CUR_W + x] = pix;
        }
    }
}

/* ------------------------------------------------------------------ */
/* mouse_handle_byte                                                  */
/* ------------------------------------------------------------------ */
void mouse_handle_byte(uint8_t data) {
    if (!mouse_ok) return;
    if (pkt_i == 0 && !(data & 0x08)) return;
    pkt[pkt_i++] = data;
    if (pkt_i < 3) return;
    pkt_i = 0;
    if (pkt[0] & 0xC0) return;

    uint8_t pressed = (pkt[0] & 7) & ~g_mouse.buttons;
    g_mouse.buttons = pkt[0] & 0x07;

    int32_t dx = (int32_t)pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
    int32_t dy = (int32_t)pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
    dy = -dy;

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
    pkt_i = 0; mouse_ok = 0;

    cursor_surface_init();

    if (!ps2_init()) return;

    mouse_ok =
        ps2_device_command(1, 0xF6) &&
        ps2_device_command(1, 0xE6) &&
        ps2_device_command(1, 0xF3) && ps2_device_command(1, 200) &&
        ps2_device_command(1, 0xE8) && ps2_device_command(1, 3)   &&
        ps2_device_command(1, 0xF4);

    if (cursor_surf) {
        cursor_surf->x = g_mouse.x;
        cursor_surf->y = g_mouse.y;
        cursor_surf->visible = mouse_ok;
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