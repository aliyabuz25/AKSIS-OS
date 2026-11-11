/*
 * SHENLONG OS - Ana Kernel (Limine Boot Protocol v3)
 */

#include "kernel.h"
#include "fb.h"
#include "fs.h"
#include "kernel_info.h"
#include "keyboard.h"
#include "mouse.h"
#include "ps2.h"
#include "splash.h"
#include "gpu.h"
#include "ui.h"
#include "pat.h"
#include "net.h"
#include "audio.h"
#include "ata.h"
#include "ssh.h"
#include "wddm.h"
#include "dwm.h"
#include "d3d.h"
#include "gdi.h"
#include "wpf.h"
#include "posix.h"
#include "process.h"
#include "limine.h"

/* ------------------------------------------------------------------ */
/* Limine section marker'ları                                         */
/* ------------------------------------------------------------------ */
__attribute__((used, section(".requests_start_marker")))
static volatile uint64_t requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile uint64_t requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

__attribute__((used, section(".requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = NULL,
};

__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bl_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID, .revision = 0, .response = NULL,
};

__attribute__((used, section(".requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0, .response = NULL,
};

static inline void dbg_putc(char c) { outb(0x3f8, c); }
static void dbg_puts(const char *s) { while (*s) dbg_putc(*s++); }

/* ------------------------------------------------------------------ */
/* Kernel giriş noktası                                               */
/* ------------------------------------------------------------------ */
void kernel_main(void) {
    dbg_puts("[KERNEL] Entering kernel_main\n");
    /* Framebuffer al */
    if (!fb_request.response || fb_request.response->framebuffer_count == 0)
        for (;;) __asm__ volatile("cli; hlt");

    struct limine_framebuffer *fb = fb_request.response->framebuffers[0];

    /* IDT + heap ÖNCE — fb_init gpu_init icinden kmalloc cagiriyor */
    idt_init();
    mem_init();
    dbg_puts("[KERNEL] mem_init done\n");
    /* pat_init() — simdilik devre disi */

    /* Framebuffer + GPU compositor */
    fb_init(fb->address, (uint32_t)fb->width, (uint32_t)fb->height,
            (uint32_t)fb->pitch, fb->bpp);
    fb_clear(0);

    /* Windows Grafikleri Mimarisi: WDDM, DWM, Direct3D ve WPF */
    wddm_init(fb->address, (uint32_t)fb->width, (uint32_t)fb->height,
              (uint32_t)fb->pitch, fb->bpp);
    dwm_init(fb->address, (uint32_t)fb->width, (uint32_t)fb->height,
             (uint32_t)fb->pitch, fb->bpp);
    wpf_init();

    /* Sistem bilgilerini topla */
    sysinfo_init();
    g_sysinfo.fb_width  = (uint32_t)fb->width;
    g_sysinfo.fb_height = (uint32_t)fb->height;
    g_sysinfo.fb_bpp    = fb->bpp;

    /* Bootloader bilgisi */
    if (bl_request.response) {
        g_sysinfo.bootloader_name    = bl_request.response->name;
        g_sysinfo.bootloader_version = bl_request.response->version;
    }

    /* Boot mesajı */
    fb_puts("SHENLONG OS v7.1 - booting...\n");

    /* ATA/IDE Sürücüsü & Kalıcı Dosya Sistemi */
    ata_init();
    fs_init();
    fs_mount_disk();
    process_init();
    posix_init();
    perm_init();
    tmpfs_mount("/tmp");
    tmpfs_mount("/sys");

    /* The shared i8042 queue is serviced by polling; keep IRQs disabled. */
    __asm__ volatile("cli");
    if (ps2_init()) {
        kbd_init();
    }

    mouse_init();
    net_init();
    audio_init();
    ssh_init();
    dbg_puts("[KERNEL] Starting splash_show\n");
    splash_show();
    dbg_puts("[KERNEL] splash_show finished\n");

    /* GPU compositor + UI sistemi */
    dbg_puts("[KERNEL] Starting ui_init\n");
    ui_init();
    dbg_puts("[KERNEL] ui_init finished\n");
    /* Terminal viewport: taskbar alanını dışarıda bırak */
    ui_flush();
    dbg_puts("[KERNEL] ui_flush finished\n");

    extern void shell_main(void);
    dbg_puts("[KERNEL] Entering shell_main\n");
    shell_main();

    kernel_panic("Shell sonlandi!");
}

void kernel_panic(const char *msg) {
    dbg_puts("\n[PANIC] ");
    dbg_puts(msg ? msg : "unknown");
    dbg_puts("\n");
    /* Ekranı koyu kırmızıya boya */
    if (gpu_fb && gpu_fb_w && gpu_fb_h) {
        uint32_t total = gpu_fb_pitch * gpu_fb_h / 4;
        uint32_t *p = (uint32_t *)gpu_fb;
        for (uint32_t i = 0; i < total; i++) p[i] = 0xFF0A0000u;
    }

    /* Panic logosu — ortaya */
    extern const uint8_t  panic_logo_data[];
    extern const uint32_t panic_logo_size;
    if (gpu_fb && panic_logo_size > 8) {
        uint32_t lw, lh;
        __builtin_memcpy(&lw, panic_logo_data,     4);
        __builtin_memcpy(&lh, panic_logo_data + 4, 4);
        const uint8_t *rgb = panic_logo_data + 8;
        int32_t ox = ((int32_t)gpu_fb_w - (int32_t)lw) / 2;
        int32_t oy = ((int32_t)gpu_fb_h - (int32_t)lh) / 2;
        for (uint32_t y = 0; y < lh; y++) {
            for (uint32_t x = 0; x < lw; x++) {
                int32_t px = ox + (int32_t)x;
                int32_t py = oy + (int32_t)y;
                if (px < 0 || py < 0 || (uint32_t)px >= gpu_fb_w || (uint32_t)py >= gpu_fb_h) continue;
                const uint8_t *c = rgb + (y * lw + x) * 3;
                uint32_t col = 0xFF000000u | ((uint32_t)c[0]<<16) | ((uint32_t)c[1]<<8) | c[2];
                uint8_t *dst = gpu_fb + (size_t)(uint32_t)py * gpu_fb_pitch + (size_t)(uint32_t)px * gpu_fb_bpx;
                if (gpu_fb_bpx == 4) *(uint32_t*)dst = col;
                else { dst[0]=c[0]; dst[1]=c[1]; dst[2]=c[2]; }
            }
        }
    }

    /* Sol üst — sade açıklama */
    fb_row = 1; fb_col = 2;
    fb_set_fg(0xFF6666u);
    fb_puts("SYSTEM ERROR\n");
    fb_set_fg(0xCCCCCCu);
    fb_puts(msg);
    fb_puts("\n\nSistemi yeniden baslatmak icin guc tusunu kullanin.");
    fb_present();
    for (;;) __asm__ volatile("cli; hlt");
}
