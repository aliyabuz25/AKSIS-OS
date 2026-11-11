/* Original artwork, real startup diagnostics and an 18-second boot display. */
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

#define BOOT_DURATION_MS 18000u
#define BOOT_BLUE 0xff389dffu

static gpu_surface_t *screen;
static int row,skip,pad,log_top,line_height;
static int rtc_start,timer_ok,last_second,last_width;
static uint64_t started,last_update;

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
static void progress(uint64_t elapsed) {
    if(elapsed>BOOT_DURATION_MS) elapsed=BOOT_DURATION_MS;
    int width=screen->w-pad*2;
    int filled=(uint64_t)width*elapsed/BOOT_DURATION_MS;
    int remaining=(BOOT_DURATION_MS-elapsed+999)/1000;
    int y=screen->h-116;
    if(filled!=last_width) {
        gpu_fill_rect(screen,pad,y,width,3,UI_LINE);
        if(filled) gpu_fill_rect(screen,pad,y,filled,3,BOOT_BLUE);
        gpu_surface_damage(screen,pad,y,width,3);
        last_width=filled;
    }
    if(remaining!=last_second) {
        char text[80],number[24];
        kstrcpy(text,"Masaüstüne geçiş · "); kitoa(remaining,number);
        kstrcat(text,number); kstrcat(text," sn");
        gpu_fill_rect(screen,pad,y-44,420,30,UI_BG);
        ui_text(screen,pad,y-44,text,UI_TEXT,0);
        gpu_surface_damage(screen,pad,y-44,420,30);
        last_second=remaining;
    }
}
static void pump(void) {
    ps2_poll(); net_poll();
    while(kbd_poll()) {
        char key=kbd_getc();
        if(key=='\n'||key==KBD_ESCAPE) skip=1;
    }
    uint64_t elapsed=elapsed_ms();
    if(elapsed-last_update>=33) { progress(elapsed); last_update=elapsed; }
    gpu_compose();
    __asm__ volatile("pause");
}
static void pause_ms(unsigned ms) {
    uint64_t until=elapsed_ms()+ms;
    while(timer_ok && !skip && elapsed_ms()<until) pump();
}
static void log_value(int index,const char *value,int good) {
    int y=log_top+index*line_height;
    gpu_fill_rect(screen,pad+322,y,300,28,UI_BG);
    ui_text(screen,pad+322,y,value,good?UI_ACCENT:UI_MUTED,0);
    gpu_surface_damage(screen,pad+322,y,300,28);
}
static void log_line(const char *name,const char *value,int good) {
    char stamp[32],number[24]; uint64_t ms=elapsed_ms();
    kstrcpy(stamp,"[ "); kitoa(ms,number); kstrcat(stamp,number); kstrcat(stamp," ms ]");
    int y=log_top+row*line_height;
    ui_text(screen,pad,y,stamp,UI_MUTED,0);
    ui_text(screen,pad+128,y,name,UI_TEXT,0);
    log_value(row,value,good);
    gpu_surface_damage(screen,pad,y,622,28);
    gpu_compose(); row++; pause_ms(70);
}
void splash_show(void) {
    screen=gpu_surface_create(gpu_fb_w,gpu_fb_h,0,0,900);
    if(!screen) return;
    row=skip=0; last_second=last_width=-1; last_update=0;
    pad=gpu_fb_w>=1440?64:32;
    log_top=gpu_fb_h>=900?260:164;
    line_height=gpu_fb_h>=900?36:28;
    gpu_fill(screen,UI_BG);
    /* At Full HD the original 3:2 image occupies 1280x853 on the right.
       Keep its aspect ratio and all RGB detail; render/filter only once. */
    if(gpu_fb_w>=1100)
        boot_logo_draw(screen,640,24,gpu_fb_w-640,gpu_fb_h-200);
    else
        boot_logo_draw(screen,gpu_fb_w-240,0,240,150);
    ui_text(screen,pad-2,64,"SHENLONG",UI_TEXT,2);
    ui_text(screen,pad,128,"NIGHT / SİSTEM AÇILIŞI",UI_MUTED,0);
    gpu_fill_rect(screen,pad,log_top-52,96,2,BOOT_BLUE);
    ui_text(screen,pad,gpu_fb_h-72,"Ayrıntılı açılış günlüğü  ·  Enter veya Esc ile geç",UI_MUTED,0);
    started=get_uptime_ms(); rtc_start=rtc_seconds();
    timer_ok=g_sysinfo.tsc_freq_hz>=1000 || rtc_start>=0;
    progress(0);
    gpu_surface_damage(screen,0,0,screen->w,screen->h); gpu_compose();
    char value[64],number[24];
    log_line("Firmware",g_sysinfo.firmware_type?"UEFI":"BIOS",1);
    log_line("Önyükleyici",g_sysinfo.bootloader_name?g_sysinfo.bootloader_name:"Bilinmiyor",!!g_sysinfo.bootloader_name);
    fmt_size(g_sysinfo.total_memory,value); log_line("Bellek",value,g_sysinfo.total_memory!=0);
    kitoa(gpu_fb_w,value); kstrcat(value," x "); kitoa(gpu_fb_h,number); kstrcat(value,number);
    log_line("Framebuffer",value,1);
    log_line("Zamanlayıcı",g_sysinfo.tsc_freq_hz>=1000?"TSC hazır":(timer_ok?"RTC yedeği":"Saat okunamadı"),timer_ok);
    log_line("Dosya sistemi","RAM / tmpfs hazır",1);
    int keyboard=ps2_init();
    log_line("Klavye",keyboard?"PS/2 / Türkçe Q":"PS/2 bulunamadı",keyboard);
    log_line("Fare",mouse_available()?"PS/2 hazır":"PS/2 bulunamadı",mouse_available());
    log_line("Sanal ağ kartı",net_info.present?"RTL8139 hazır":"Kart bulunamadı",net_info.present);
    int gateway=net_info.gateway;
    log_line("Host ağı",gateway?"Ağ geçidi erişilebilir":"Bağlantı bekleniyor",gateway);
    log_line("Masaüstü","Hazırlanıyor",1);
    while(timer_ok && !skip && elapsed_ms()<BOOT_DURATION_MS) {
        pump();
        if(gateway!=net_info.gateway) {
            gateway=net_info.gateway;
            log_value(9,gateway?"Ağ geçidi erişilebilir":"Bağlantı bekleniyor",gateway);
        }
    }
    gpu_surface_destroy(screen); screen=NULL;
}
