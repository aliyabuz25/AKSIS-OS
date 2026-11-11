/*
 * SHENLONG OS - WDDM (Windows Display Driver Model) Implementation
 * Bochs/QEMU/Parallels BGA sürücüsü, VRAM yönetimi ve mod geçişleri.
 */
#include "wddm.h"
#include "gpu.h"
#include "fb.h"
#include "kernel_info.h"

wddm_adapter_t g_wddm_adapter;

extern void dwm_resize_display(uint32_t new_w, uint32_t new_h);

static inline void bga_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static inline uint16_t bga_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

void wddm_init(void *fb_addr, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp) {
    kmemset(&g_wddm_adapter, 0, sizeof(g_wddm_adapter));

    g_wddm_adapter.fb_vaddr       = fb_addr;
    g_wddm_adapter.current_width  = w;
    g_wddm_adapter.current_height = h;
    g_wddm_adapter.current_pitch  = pitch;
    g_wddm_adapter.current_bpp    = bpp;

    /* Bochs Graphics Adapter (BGA) / Parallels Display denetleyicisini yokla */
    uint16_t id = bga_read(VBE_DISPI_INDEX_ID);
    if ((id & 0xFFF0) == 0xB0C0) {
        g_wddm_adapter.bga_available = 1;
        g_wddm_adapter.bga_version   = id;

        /* VRAM boyutunu oku (64KB bloklar halinde) */
        uint16_t vram_64k = bga_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
        if (vram_64k > 0) {
            g_wddm_adapter.vram_bytes = (uint32_t)vram_64k * 64 * 1024;
        } else {
            g_wddm_adapter.vram_bytes = 16 * 1024 * 1024; /* Varsayılan 16MB */
        }
        kstrcpy(g_wddm_adapter.name, "Parallels / Bochs Video Adapter (WDDM 2.0)");
    } else {
        g_wddm_adapter.bga_available = 0;
        g_wddm_adapter.bga_version   = 0;
        g_wddm_adapter.vram_bytes    = pitch * h;
        kstrcpy(g_wddm_adapter.name, "UEFI / VESA Standart Framebuffer");
    }

    /* Desteklenen standart modlar — 4K (UHD) öncelikli; VRAM'ı yetersiz
       adaptor'lar 3840x2160 için BGA geçişini kendisi reddeder. */
    const wddm_mode_t standard_modes[] = {
        { 0, 3840, 2160, 32, 60, "3840 x 2160 (UHD 4K - Onereilen)" },
        { 1, 2560, 1440, 32, 60, "2560 x 1440 (QHD 1440p)" },
        { 2, 1920, 1080, 32, 60, "1920 x 1080 (Full HD 1080p)" },
        { 3, 1600,  900, 32, 60, "1600 x 900 (HD+ 16:9)" },
        { 4, 1440,  900, 32, 60, "1440 x 900 (WXGA+ 16:10)" },
        { 5, 1280,  720, 32, 60, "1280 x 720 (HD 720p)" },
        { 6, 1024,  768, 32, 60, "1024 x 768 (XGA 4:3)" },
        { 7,  800,  600, 32, 60, "800 x 600 (SVGA)" }
    };

    int count = (int)(sizeof(standard_modes) / sizeof(standard_modes[0]));
    if (count > WDDM_MAX_MODES) count = WDDM_MAX_MODES;
    g_wddm_adapter.mode_count = count;

    for (int i = 0; i < count; i++) {
        g_wddm_adapter.modes[i] = standard_modes[i];
    }

    g_wddm_adapter.current_mode_index = wddm_find_mode(w, h);
}

/* BGA'da hedef mod için yeterince VRAM var mı? 32 bpp için
   width*height*4 bayt gerekli. */
static int bga_vram_ok(uint32_t width, uint32_t height) {
    uint64_t need = (uint64_t)width * height * 4;
    return need <= (uint64_t)g_wddm_adapter.vram_bytes;
}

int wddm_is_hardware_switch_supported(void) {
    return g_wddm_adapter.bga_available;
}

int wddm_get_mode_count(void) {
    return g_wddm_adapter.mode_count;
}

const wddm_mode_t *wddm_get_mode(int index) {
    if (index < 0 || index >= g_wddm_adapter.mode_count) return NULL;
    return &g_wddm_adapter.modes[index];
}

const wddm_mode_t *wddm_get_current_mode(void) {
    int idx = g_wddm_adapter.current_mode_index;
    if (idx >= 0 && idx < g_wddm_adapter.mode_count) {
        return &g_wddm_adapter.modes[idx];
    }
    return NULL;
}

int wddm_find_mode(uint32_t width, uint32_t height) {
    for (int i = 0; i < g_wddm_adapter.mode_count; i++) {
        if (g_wddm_adapter.modes[i].width == width &&
            g_wddm_adapter.modes[i].height == height) {
            return i;
        }
    }
    return -1;
}

int wddm_set_mode(uint32_t width, uint32_t height) {
    if (width < 640 || height < 480 || width > 3840 || height > 2160) return 0;

    /* BGA varsa VRAM sınırını kontrol et — 4K'ı desteklemeyen sanal
       adaptor'de 32 bpp mod, framebuffer'ı bozar. */
    if (g_wddm_adapter.bga_available && !bga_vram_ok(width, height))
        return 0;

    if (g_wddm_adapter.current_width == width && g_wddm_adapter.current_height == height) {
        return 1; /* Zaten bu modda */
    }

    if (g_wddm_adapter.bga_available) {
        /* BGA denetleyicisini devre dışı bırak, çözünürlüğü güncelle, etkinleştir */
        bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        bga_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
        bga_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
        bga_write(VBE_DISPI_INDEX_BPP, 32);
        bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

        /* Doğrula */
        uint16_t real_w = bga_read(VBE_DISPI_INDEX_XRES);
        uint16_t real_h = bga_read(VBE_DISPI_INDEX_YRES);
        if (real_w != (uint16_t)width || real_h != (uint16_t)height) {
            return 0;
        }
    }

    g_wddm_adapter.current_width  = width;
    g_wddm_adapter.current_height = height;
    g_wddm_adapter.current_pitch  = width * 4;
    g_wddm_adapter.current_bpp    = 32;
    g_wddm_adapter.current_mode_index = wddm_find_mode(width, height);

    /* DWM pencere birleştiriciye ve masaüstüne bildir */
    dwm_resize_display(width, height);

    return 1;
}

int wddm_cycle_mode(void) {
    int next_idx = (g_wddm_adapter.current_mode_index + 1) % g_wddm_adapter.mode_count;
    const wddm_mode_t *m = &g_wddm_adapter.modes[next_idx];
    return wddm_set_mode(m->width, m->height);
}
