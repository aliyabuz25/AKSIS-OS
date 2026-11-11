/* Minimal, quiet boot splash. Diagnostics stay on the disabled serial channel. */
#include "splash.h"
#include "boot_logo.h"
#include "ui_font.h"
#include "theme.h"
#include "kernel_info.h"
#include "keyboard.h"
#include "mouse.h"
#include "ps2.h"
#include "net.h"
#include "clock.h"

#define BOOT_DURATION_MS 3500u
#define BOOT_BLUE 0xff0284c7u
#define BOOT_BG 0xffffffffu
#define BOOT_TEXT 0xff0f172au
#define BOOT_MUTED 0xff64748bu
#define BOOT_ACCENT 0xff0284c7u
static gpu_surface_t *screen;
static int row,skip,verbose,pad,log_top,line_height;
static int rtc_start,timer_ok,last_second,last_width;
static uint64_t started,last_update;
static struct { const char *name; char value[64]; int good; uint64_t ms; } logs[16];

static int rtc_seconds(void) {
    wall_time_t now;
    if(!clock_read(&now,0)) return -1;
    return now.hour*3600+now.minute*60+now.second;
}
static uint64_t elapsed_ms(void) {
    if(g_sysinfo.tsc_freq_hz>=1000) return get_uptime_ms()-started;
    int now=rtc_seconds();
    if(now<0) { timer_ok=0; return BOOT_DURATION_MS; }
    return (unsigned)((now-rtc_start+86400)%86400)*1000u;
}
/* 64-entry fixed-point trigonometri tablosu (sin * 256) */
static const int16_t s_sin64[64] = {
      0,  25,  49,  74,  97, 120, 142, 162,
    181, 197, 212, 224, 235, 244, 250, 254,
    256, 254, 250, 244, 235, 224, 212, 197,
    181, 162, 142, 120,  97,  74,  49,  25,
      0, -25, -49, -74, -97,-120,-142,-162,
   -181,-197,-212,-224,-235,-244,-250,-254,
   -256,-254,-250,-244,-235,-224,-212,-197,
   -181,-162,-142,-120, -97, -74, -49, -25
};

static inline int16_t get_sin(unsigned step) {
    return s_sin64[step & 63];
}
static inline int16_t get_cos(unsigned step) {
    return s_sin64[(step + 16) & 63];
}

static void draw_ball(gpu_surface_t *s, int bx, int by, int r, uint32_t color) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                gpu_put_pixel(s, bx + dx, by + dy, color);
            }
        }
    }
}

static void progress(uint64_t elapsed) {
    if(elapsed>BOOT_DURATION_MS) elapsed=BOOT_DURATION_MS;

    int cx = (int)screen->w / 2;
    int cy = (int)screen->h / 2 + 105;

    /* Eski progressbar çubuğu kaldırıldı: Yalnızca dairesel dönen beyaz toplar */
    gpu_fill_rect(screen, cx - 45, cy - 45, 90, 90, BOOT_BG);

    unsigned base_step = (unsigned)(elapsed / 24) & 63;
    for (int i = 0; i < 8; i++) {
        unsigned step = (base_step + i * 8) & 63;
        int cos_v = get_cos(step);
        int sin_v = get_sin(step);
        int bx = cx + (cos_v * 26) / 256;
        int by = cy + (sin_v * 26) / 256;

        uint32_t col;
        int r = 2;
        if (i == 0)      { col = 0xff0284c7u; r = 3; }
        else if (i == 1) { col = 0xd00284c7u; r = 3; }
        else if (i == 2) { col = 0xa00284c7u; r = 2; }
        else if (i == 3) { col = 0x780284c7u; r = 2; }
        else if (i == 4) { col = 0x550284c7u; r = 2; }
        else if (i == 5) { col = 0x380284c7u; r = 2; }
        else if (i == 6) { col = 0x220284c7u; r = 1; }
        else             { col = 0x140284c7u; r = 1; }

        draw_ball(screen, bx, by, r, col);
    }
    gpu_surface_damage(screen, cx - 45, cy - 45, 90, 90);
}

static void paint_log(int i) {
    if (i < 0 || i >= 16) return;
    char stamp[32], number[24];
    kstrcpy(stamp, "[ ");
    kitoa(logs[i].ms, number);
    kstrcat(stamp, number);
    kstrcat(stamp, " ms ]");
    int ly = 48 + i * 22;
    gpu_fill_rect(screen, 24, ly, 460, 20, BOOT_BG);
    ui_text(screen, 24, ly, stamp, 0xff94a3b8u, 0);
    ui_text(screen, 120, ly, logs[i].name, 0xff1e293bu, 0);
    ui_text(screen, 250, ly, logs[i].value, logs[i].good ? 0xff0284c7u : 0xff64748bu, 0);
    gpu_surface_damage(screen, 24, ly, 460, 20);
}

static void scene(void) {
    gpu_fill(screen, BOOT_BG);
    /* Merkezde sade ve mat dragon boot logosu */
    boot_logo_draw(screen, (int)gpu_fb_w / 2 - 240, (int)gpu_fb_h / 2 - 235, 480, 320);

    progress(elapsed_ms());
    gpu_surface_damage(screen, 0, 0, screen->w, screen->h);
}

static void pump(void) {
    ps2_poll(); net_poll();
    while(kbd_poll()) {
        char key=kbd_getc();
        if(key=='\n'||key==KBD_ESCAPE||key==' ') skip=1;
    }
    uint64_t elapsed=elapsed_ms();
    if(elapsed-last_update>=20) {
        progress(elapsed);
        last_update=elapsed;
        gpu_compose();
    }
    __asm__ volatile("pause");
}

static void pause_ms(unsigned ms) {
    if (g_sysinfo.tsc_freq_hz >= 1000) {
        uint64_t until=elapsed_ms()+ms;
        while(timer_ok && !skip && elapsed_ms()<until) pump();
    } else {
        pump();
    }
}

static __attribute__((unused)) void log_line(const char *name,const char *value,int good) {
    if(row>=16) return;
    logs[row].name=name; kstrcpy(logs[row].value,value);
    logs[row].good=good; logs[row].ms=elapsed_ms();
    paint_log(row);
    row++; pause_ms(45);
}

static inline void dbg_putc(char c) { __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3f8)); }
static void dbg_puts(const char *s) { while (*s) dbg_putc(*s++); }

void splash_show(void) {
    dbg_puts("[SPLASH] show started\n");
    screen=gpu_surface_create(gpu_fb_w,gpu_fb_h,0,0,900);
    if(!screen) return;
    row=skip=verbose=0; last_second=last_width=-1; last_update=0;
    pad=gpu_fb_w>=1440?64:32;
    log_top=gpu_fb_h>=900?260:164;
    line_height=gpu_fb_h>=900?36:(gpu_fb_h>=720?28:22);
    mouse_set_visible(0);
    started=get_uptime_ms(); rtc_start=rtc_seconds();
    timer_ok=g_sysinfo.tsc_freq_hz>=1000 || rtc_start>=0;
    scene(); gpu_compose();
    /* Quiet startup: show only the logo and a short progress animation. */
    while(timer_ok && !skip && elapsed_ms()<1200u) pump();
    dbg_puts("[SPLASH] show loop ended\n");
    gpu_surface_destroy(screen); screen=NULL;
    dbg_puts("[SPLASH] screen destroyed\n");
    g_mouse.pressed=0; mouse_set_visible(1);
}
