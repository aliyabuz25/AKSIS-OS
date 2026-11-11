/*
 * SHENLONG OS - WDDM (Windows Display Driver Model) Abstraction
 * Donanım bağdaştırıcı soyutlaması, video bellek yönetimi ve dinamik mod yönetimi.
 */
#ifndef SHENLONG_WDDM_H
#define SHENLONG_WDDM_H

#include "kernel.h"

#define WDDM_MAX_MODES 10

/* Bochs Graphics Adapter (BGA) VBE Dispi Register Tanımları */
#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0
#define VBE_DISPI_INDEX_XRES             1
#define VBE_DISPI_INDEX_YRES             2
#define VBE_DISPI_INDEX_BPP              3
#define VBE_DISPI_INDEX_ENABLE           4
#define VBE_DISPI_INDEX_BANK             5
#define VBE_DISPI_INDEX_VIRT_WIDTH       6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      7
#define VBE_DISPI_INDEX_X_OFFSET         8
#define VBE_DISPI_INDEX_Y_OFFSET         9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 10

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

typedef struct {
    int         id;
    uint32_t    width;
    uint32_t    height;
    uint16_t    bpp;
    uint16_t    refresh_rate;
    const char *name;
} wddm_mode_t;

typedef struct {
    char        name[64];
    uint32_t    vram_bytes;
    int         bga_available;
    uint16_t    bga_version;
    uint32_t    current_width;
    uint32_t    current_height;
    uint32_t    current_pitch;
    uint16_t    current_bpp;
    void       *fb_vaddr;
    int         mode_count;
    wddm_mode_t modes[WDDM_MAX_MODES];
    int         current_mode_index;
} wddm_adapter_t;

/* Global WDDM bağdaştırıcı durumu */
extern wddm_adapter_t g_wddm_adapter;

/* WDDM API */
void                wddm_init(void *fb_addr, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp);
int                 wddm_is_hardware_switch_supported(void);
int                 wddm_get_mode_count(void);
const wddm_mode_t  *wddm_get_mode(int index);
const wddm_mode_t  *wddm_get_current_mode(void);
int                 wddm_find_mode(uint32_t width, uint32_t height);
int                 wddm_set_mode(uint32_t width, uint32_t height);
int                 wddm_cycle_mode(void);

#endif /* SHENLONG_WDDM_H */
