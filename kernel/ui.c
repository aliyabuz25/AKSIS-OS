/* SHENLONG native desktop with Windows 10 layout and retained surfaces. */
#include "ui.h"
#include "ui_font.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "kernel_info.h"
#include "clock.h"
#include "fs.h"
#include "theme.h"
#include "icons.h"
#include "net.h"
#include "browser.h"
#include "media.h"
#include "wallpaper.h"
#include "boot_logo.h"
#include "ata.h"
#include "ui_paint.h"
#include "html_ui.h"
#include "axis_web.h"
#include "desktop_layout.h"
#include "wddm.h"
#include "dwm.h"
#include "d3d.h"
#include "gdi.h"
#include "wpf.h"
#include "start_icon_data.h"
#include "ai.h"

#define TASKBAR_H 48
#define TOP_H 0
#define BAR_H TASKBAR_H
#define TITLE_H 44
#define DESKTOP_COUNT 2
static const int desktop_apps[]={1,2};
#define INK UI_TEXT
#define DIM UI_MUTED
#define BLUE UI_ACCENT
#define PANEL UI_PANEL
#define LINE UI_LINE
#define APP_COUNT 6

/* DPI ölçek faktörü — 1080p HD default, 4K destekli. 8-bit sabit nokta
   (256 = 1.0). 1080p boot'te mevcut görünüm değişmez. */
static unsigned ui_scale = 256;
static int ui_scale_px(int n) { return (n * (int)ui_scale) >> 8; }
static void ui_update_scale(int sw) {
    if (sw >= 3200)      ui_scale = 512; /* 2.0x */
    else if (sw >= 2000) ui_scale = 384; /* 1.5x */
    else                 ui_scale = 256; /* 1.0x */
}
/* Ölçeklenmiş taskbar yüksekliği — 4K'ta 96px, 1080p'de 48px */
static int taskbar_h_scaled(void) { return ui_scale_px(TASKBAR_H); }

static desktop_layout_t shell_layout;

typedef struct {
    int open, minimized, maximized, x,y,w,h,rx,ry,rw,rh;
    int painted_w, painted_h, painted_focus, painted_hover, content_dirty;
    gpu_surface_t *surface, *shadow;
} window_t;
static window_t windows[APP_COUNT];
static int transition_app=-1;
static void sync_windows(void);
static void calendar_set(int open);
static void wallpaper_draw(void);
static void desktop_icons_draw(void);
static const char *names[]={"Terminal","Dosyalar","Sistem","Jade Tarayıcı","Ağ","Medya"};
static int order[APP_COUNT]={0,1,2,3,4,5}, focused=-1;
static gpu_surface_t *wallpaper,*bar,*menu,*calendar,*context,*desktop_icons,*marquee_surf;
static int marquee_active=0, marquee_start_x=0, marquee_start_y=0, marquee_cur_x=0, marquee_cur_y=0;
static int marquee_old_rx=0, marquee_old_ry=0, marquee_old_rw=0, marquee_old_rh=0;
static uint32_t desktop_selected_mask=0;
static int sw,sh,menu_x,menu_y,menu_w,menu_h,calendar_x,calendar_y;
static int menu_open,calendar_open,context_open,dirty,dragging=-1,drag_dx,drag_dy;
static int context_x,context_y,context_hover=-1,context_paint=1;
static int keyboard_menu=0,desktop_last_app=-1;
static uint64_t desktop_last_click;
static int offset_minutes=180,hover=-1,menu_choice;
static int dock_dirty=1, file_scroll;
static int calendar_from, calendar_value;
static uint64_t next_ui_frame;

/* Animasyon state */
#define ANIM_MS 180
static int menu_anim=0, menu_closing=0;
static int calendar_anim=0, calendar_closing=0;
static int win_anim[APP_COUNT];
static uint64_t menu_started, calendar_started, win_started[APP_COUNT], last_animation, next_clock;
static int menu_paint=1, calendar_paint=1;
static uint64_t last_bar_signature=(uint64_t)-1;
static unsigned last_network_signature=(unsigned)-1;
static char search[48];
static int search_len;
static wall_time_t wall_time;
static int last_second=-1,time_valid;
static unsigned clock_poll;
static vfs_node_t *file_dir,*file_preview;

#define CONTEXT_W 292
#define CONTEXT_H 240
#define CONTEXT_ITEM_H 36
#define CONTEXT_FIRST 16
#define CONTEXT_REFRESH 800
#define CONTEXT_WALLPAPER 801
#define CONTEXT_DISPLAY 802
#define CONTEXT_PERSONALIZE 803
#define CONTEXT_TASKBAR 804

static int inside(int px,int py,int x,int y,int w,int h) {
    return px>=x && py>=y && px<x+w && py<y+h;
}
static int clamp(int n,int a,int b) { if(b<a) b=a; return n<a?a:(n>b?b:n); }
static void fill(gpu_surface_t *s,int x,int y,int w,int h,uint32_t c) {
    if(w>0&&h>0) gpu_fill_rect(s,x,y,w,h,c);
}
static void rounded(gpu_surface_t *s,int x,int y,int w,int h,int r,uint32_t c) {
    ui_round(s,x,y,w,h,r,c);
}

static void label(gpu_surface_t *s,int x,int y,const char *str,uint32_t c) {
    ui_text(s,x,y,str,c,0);
}
static void center(gpu_surface_t *s,int x,int y,int width,const char *str,uint32_t c) {
    label(s,x+(width-ui_text_measure(str,0))/2,y,str,c);
}
static void number(gpu_surface_t *s,int x,int y,uint64_t n,const char *suffix) {
    char b[64]; kitoa(n,b); kstrcat(b,suffix); label(s,x,y,b,INK);
}
static void icon(gpu_surface_t *s,int x,int y,int app,int size) {
    ui_icon(s,x,y,app,size);
}
static int file_icon(const vfs_node_t *node) {
    if (!node || node->type == NODE_DIR) return 6;
    const char *n=node->name; int dot=-1;
    for (int i=0; n[i]; i++) if (n[i]=='.') dot=i;
    if (dot>=0) {
        const char *e=n+dot+1;
        if (kstrcmp(e,"png")==0 || kstrcmp(e,"jpg")==0 || kstrcmp(e,"jpeg")==0 || kstrcmp(e,"bmp")==0) return 8;
        if (kstrcmp(e,"bin")==0 || kstrcmp(e,"elf")==0 || kstrcmp(e,"sh")==0) return 9;
    }
    return 7;
}

/* Opaque popup body: stable contrast and cheap compositor occlusion. */
static void panel(gpu_surface_t *s) {
    gpu_fill(s,UI_PANEL);
    gpu_draw_rect(s,0,0,s->w,s->h,UI_LINE,1);
    s->opaque=1; s->opaque_inset=0;
}

static void glyph_search(gpu_surface_t *s,int x,int y,uint32_t c) {
    ui_round(s,x,y,12,12,6,c);
    ui_round(s,x+2,y+2,8,8,4,0xffffffffu);
    gpu_draw_line(s,x+10,y+10,x+16,y+16,c);
    gpu_draw_line(s,x+11,y+10,x+17,y+16,c);
}
static void start_button_draw(gpu_surface_t *s, int bx, int by, int bw, int bh, int hover, int open) {
    if (!s || bw <= 0 || bh <= 0) return;

    /* Subtle frosted glass backplate (card inset) */
    int card_x = bx + 4;
    int card_y = by + 4;
    int card_w = bw - 8; /* 40 */
    int card_h = bh - 8; /* 40 */
    int radius = 8;

    uint32_t glass_bg;
    uint32_t border_col;
    uint32_t top_sheen;
    uint32_t icon_tint;

    if (open) {
        /* Active / Start menu open: Vibrant accent acrylic glass */
        glass_bg = 0x400284c7u;
        border_col = 0x800284c7u;
        top_sheen = 0xd0ffffffu;
        icon_tint = 0xff0284c7u;
    } else if (hover) {
        /* Hover: Luminous frosted glass glow */
        glass_bg = 0x250284c7u;
        border_col = 0x500284c7u;
        top_sheen = 0xb0ffffffu;
        icon_tint = 0xff0284c7u;
    } else {
        /* Normal: Subtle elegant translucent glass */
        glass_bg = 0x14000000u;
        border_col = 0x20000000u;
        top_sheen = 0x60ffffffu;
        icon_tint = 0xff1e293bu;
    }

    /* Fill rounded card */
    ui_round(s, card_x, card_y, card_w, card_h, radius, glass_bg);

    /* Top-half glossy reflection gradient for realistic curved glass lens */
    int gloss_h = card_h / 2;
    for (int y = 0; y < gloss_h; y++) {
        uint32_t a = (open ? 40 : (hover ? 34 : 24)) * (gloss_h - y) / gloss_h;
        if (a > 0) {
            uint32_t gloss_col = GPU_RGBA(255, 255, 255, a);
            int inset = (y < 4) ? (4 - y) : 1;
            int rx = card_x + inset;
            int rw = card_w - inset * 2;
            if (rw > 0) {
                gpu_fill_rect(s, rx, card_y + y, rw, 1, gloss_col);
            }
        }
    }

    /* 1px glass border highlight */
    gpu_draw_rect(s, card_x, card_y, card_w, card_h, border_col, 1);

    /* Top specular bevel sheen */
    gpu_fill_rect(s, card_x + radius, card_y, card_w - radius * 2, 1, top_sheen);

    /* Render Dragon Icon centered inside the glass plate */
    int icon_x = card_x + (card_w - 24) / 2;
    int icon_y = card_y + (card_h - 24) / 2;

    uint32_t ir = GPU_R(icon_tint);
    uint32_t ig = GPU_G(icon_tint);
    uint32_t ib = GPU_B(icon_tint);

    for (int py = 0; py < 24; py++) {
        for (int px = 0; px < 24; px++) {
            uint32_t a = dragon_start_icon[py * 24 + px];
            if (a > 8) {
                uint32_t col = GPU_RGBA(ir, ig, ib, a);
                gpu_put_pixel(s, icon_x + px, icon_y + py, col);
            }
        }
    }
}
static void glyph_power(gpu_surface_t *s,int x,int y,uint32_t c) {
    ui_round(s,x,y+2,18,18,9,c);
    ui_round(s,x+2,y+4,14,14,7,UI_PANEL);
    fill(s,x+6,y,6,7,UI_PANEL);
    fill(s,x+8,y,2,10,c);
}
static void fit_label(gpu_surface_t *s,desktop_rect_t r,int inset,int y,const char *text,uint32_t c) {
    ui_text_fit(s,r.x+inset,r.y+y,r.w-inset*2,text,c,0);
}

/* Windows-style desktop context menu. It is a retained surface, so opening
   it never forces the wallpaper or application windows to be repainted. */
static void context_draw(void) {
    if(!context) return;
    panel(context);
    const char *items[] = {
        "Yenile",
        "Duvar kağıdını değiştir",
        "Ekran bilgisi",
        "Sistem",
        "Başlat menüsü"
    };
    const int ids[] = { CONTEXT_REFRESH, CONTEXT_WALLPAPER, CONTEXT_DISPLAY,
                        CONTEXT_PERSONALIZE, CONTEXT_TASKBAR };
    for(int i=0;i<5;i++) {
        int yy=CONTEXT_FIRST+i*CONTEXT_ITEM_H;
        if(context_hover==ids[i])
            fill(context,10,yy,CONTEXT_W-20,CONTEXT_ITEM_H-2,UI_HOVER);
        ui_icon_nobg(context,22,yy+7,i==0?7:(i==1?8:2),20);
        label(context,54,yy+10,items[i],context_hover==ids[i]?UI_ACCENT:INK);
        if(i==1) fill(context,18,yy+CONTEXT_ITEM_H-1,CONTEXT_W-36,1,UI_LINE);
    }
    fill(context,18,CONTEXT_H-42,CONTEXT_W-36,1,UI_LINE);
    ui_text_fit(context,20,CONTEXT_H-28,CONTEXT_W-40,"Masaüstü",DIM,0);
    context->opaque=1; context->opaque_inset=0;
}

static void context_close(void) {
    if(!context_open) return;
    context_open=0; context_hover=-1;
    if(context) context->visible=0;
    dirty=1;
}

static void desktop_refresh(void) {
    wallpaper_draw();
    desktop_icons_draw();
    gpu_wallpaper_bake();
    dock_dirty=1;
    dirty=1;
}

static void context_open_at(int x,int y) {
    menu_open=0; calendar_set(0);
    context_x=clamp(x,8,sw-CONTEXT_W-8);
    context_y=clamp(y,8,sh-taskbar_h_scaled()-CONTEXT_H-8);
    context_open=1; context_hover=-1; context_paint=1;
    dragging=-1; marquee_active=0;
    if(marquee_surf) gpu_surface_show(marquee_surf,0);
    if(context) { context->x=context_x; context->y=context_y; context->visible=1; }
    dirty=1;
}

static int matches(int app) {
    if(!search_len) return 1;
    const char *s=names[app];
    for(int i=0;s[i];i++) {
        int j=0;
        for(;j<search_len && s[i+j];j++) {
            char a=s[i+j],b=search[j];
            if(a>='A'&&a<='Z') a+=32;
            if(b>='A'&&b<='Z') b+=32;
            if(a!=b) break;
        }
        if(j==search_len) return 1;
    }
    return 0;
}
static int first_match(void) { for(int i=0;i<APP_COUNT;i++) if(matches(i)) return i; return -1; }

static void wallpaper_draw(void) {
    /* Bright neutral desktop surface; the centered mark is the wallpaper. */
    gpu_fill(wallpaper, 0xffffffffu);
    /* Keep the SHENLONG mark visible on every desktop wallpaper. */
    boot_logo_draw(wallpaper, sw / 2 - 240, (sh-taskbar_h_scaled()) / 2 - 160, 480, 320);
}

static void desktop_icons_draw(void) {
    if(!desktop_icons) return;
    for(unsigned y=0; y<desktop_icons->h; y++) kmemset(desktop_icons->pixels + y*desktop_icons->stride, 0, desktop_icons->w * 4);
    for(int i=0;i<DESKTOP_COUNT;i++) {
        int app=desktop_apps[i],iy=8+i*104;
        int selected=(desktop_selected_mask&(1u<<app))!=0;
        if(selected || hover==600+app) {
            ui_round(desktop_icons, 4, iy, 104, 94, 10, selected ? 0x350284c7u : 0x18000000u);
            gpu_draw_rect(desktop_icons, 4, iy, 104, 94, selected ? 0x800284c7u : 0x30cbd5e1u, 1);
        }
        icon(desktop_icons,32,iy+8,app,48);
        center(desktop_icons,0,iy+66,112,names[app],INK);
    }
    gpu_surface_damage(desktop_icons,0,0,desktop_icons->w,desktop_icons->h);
}

static void taskbar_draw(void);
static void taskbar_clock_draw(void) { dock_dirty=1; }

static void taskbar_draw(void) {
    if(!bar) return;
    int th = (int)bar->h;
    /* 1x: 48px, 1.5x: 72px, 2x: 96px — dock simge yüksekliği */
    int icon_h = th - 8;
    gpu_fill(bar,0xfff1f5f9u);
    fill(bar,0,0,sw,1,UI_LINE);
    desktop_rect_t r=shell_layout.start;
    start_button_draw(bar,r.x,r.y,r.w,r.h,hover==100,menu_open);
    r=shell_layout.copilot;
    if(hover==98||ai_open) fill(bar,r.x+2,4,r.w-4,th-8,0xffe2e8f0u);
    ai_draw_circle_icon(bar,r.x+r.w/2,th/2,ui_scale_px(13),hover==98||ai_open);
    r=shell_layout.search;
    if(r.w > 0) {
        int sb_h = ui_scale_px(36), sb_y = th/2 - sb_h/2;
        fill(bar,r.x,sb_y,r.w,sb_h,0xffffffffu);
        if(hover==99) gpu_draw_rect(bar,r.x,sb_y,r.w,sb_h,UI_ACCENT,1);
        else gpu_draw_rect(bar,r.x,sb_y,r.w,sb_h,UI_LINE,1);
        glyph_search(bar,r.x+ui_scale_px(12),sb_y+ui_scale_px(8),DIM);
        if(r.w>80) ui_text_fit(bar,r.x+ui_scale_px(38),sb_y+ui_scale_px(10),r.w-ui_scale_px(50),menu_open&&search_len?search:"Aramak için yazın",DIM,0);
    }
    for(int i=0;i<APP_COUNT;i++) {
        r=shell_layout.apps[i];
        int active=focused==i&&windows[i].open&&!windows[i].minimized;
        if(active||hover==101+i) fill(bar,r.x,1,r.w,th,active?0xffe2e8f0u:0xffe8edf5u);
        icon(bar,r.x+ui_scale_px(12),ui_scale_px(6),i,icon_h);
        if(windows[i].open) fill(bar,r.x+ui_scale_px(4),th-ui_scale_px(3),r.w-ui_scale_px(8),ui_scale_px(3),active?UI_ACCENT:0xff94a3b8u);
    }
    r=shell_layout.network;
    {
        int nw=r.w, nh=th;
        int net_x=r.x+ui_scale_px(13), net_y=th/2-ui_scale_px(6);
        if(hover==112) fill(bar,r.x,1,nw,nh,0xffe2e8f0u);
        gpu_draw_rect(bar,net_x,net_y,ui_scale_px(18),ui_scale_px(12),net_info.gateway?INK:DIM,1);
        fill(bar,net_x+ui_scale_px(8),net_y+ui_scale_px(12),ui_scale_px(2),ui_scale_px(4),INK);
        fill(bar,net_x+ui_scale_px(4),net_y+ui_scale_px(16),ui_scale_px(10),ui_scale_px(1),INK);
        if(!net_info.gateway) {
            gpu_draw_line(bar,net_x+ui_scale_px(13),net_y+ui_scale_px(9),net_x+ui_scale_px(20),net_y+ui_scale_px(16),UI_DANGER);
            gpu_draw_line(bar,net_x+ui_scale_px(20),net_y+ui_scale_px(9),net_x+ui_scale_px(13),net_y+ui_scale_px(16),UI_DANGER);
        }
    }
    r=shell_layout.clock;
    if(hover==110||calendar_open) fill(bar,r.x,1,r.w,th,0xffe2e8f0u);
    char t[16],d[16];
    if(time_valid) { clock_time_text(&wall_time,t); t[5]=0; clock_date_text(&wall_time,d); }
    else { kstrcpy(t,"--:--"); kstrcpy(d,""); }
    center(bar,r.x,ui_scale_px(5),r.w,t,INK);
    center(bar,r.x,th-ui_scale_px(22),r.w,d,DIM);
    r=shell_layout.peek;
    fill(bar,r.x,1,1,th,UI_LINE);
    if(hover==111) fill(bar,r.x+1,1,r.w-1,th,0xffe2e8f0u);
    bar->opaque=1;
    gpu_surface_damage(bar,0,0,sw,th);
    dock_dirty=0;
}

static const int menu_order[]={4,1,3,5,2,0};
static void menu_draw(void) {
    panel(menu);

    /* Left rail: Windows 10 navigation column */
    int rail_w = ui_scale_px(47), rail_sep = ui_scale_px(48);
    fill(menu, 1, 1, rail_w, menu_h - 2, 0xfff1f5f9u);
    fill(menu, rail_sep, 1, 1, menu_h - 2, UI_LINE);

    /* Hamburger menu glyph at top */
    for(int i = 0; i < 3; i++) fill(menu, ui_scale_px(16), ui_scale_px(20) + i * ui_scale_px(5), ui_scale_px(16), ui_scale_px(2), INK);

    /* Left rail action buttons (bottom-up) */
    int rail_btn_w = ui_scale_px(40), rail_btn_h = ui_scale_px(38);
    int r_ai = (hover == 244);
    if(r_ai||ai_open) fill(menu, 4, menu_h - ui_scale_px(224), rail_btn_w, rail_btn_h, 0xffe2e8f0u);
    ai_draw_circle_icon(menu, ui_scale_px(24), menu_h - ui_scale_px(205), ui_scale_px(13), r_ai||ai_open);

    int r_user = (hover == 243);
    if(r_user) fill(menu, 4, menu_h - ui_scale_px(180), rail_btn_w, rail_btn_h, 0xffe2e8f0u);
    {
        int av_s = ui_scale_px(28), av_r = ui_scale_px(14);
        ui_round(menu, ui_scale_px(10), menu_h - ui_scale_px(176), av_s, av_s, av_r, 0xff0284c7u);
        center(menu, ui_scale_px(10), menu_h - ui_scale_px(170), av_s, "A", 0xffffffffu);
    }

    int r_docs = (hover == 201);
    if(r_docs) fill(menu, 4, menu_h - ui_scale_px(136), rail_btn_w, rail_btn_h, 0xffe2e8f0u);
    icon(menu, ui_scale_px(12), menu_h - ui_scale_px(130), 1, ui_scale_px(24));

    int r_sets = (hover == 202);
    if(r_sets) fill(menu, 4, menu_h - ui_scale_px(92), rail_btn_w, rail_btn_h, 0xffe2e8f0u);
    icon(menu, ui_scale_px(12), menu_h - ui_scale_px(86), 2, ui_scale_px(24));

    int r_pwr = (hover == 210);
    if(r_pwr) fill(menu, 4, menu_h - ui_scale_px(48), rail_btn_w, rail_btn_h, 0xfffee2e2u);
    glyph_power(menu, ui_scale_px(14), menu_h - ui_scale_px(40), r_pwr ? UI_DANGER : INK);

    /* Middle column: All Applications list */
    int mid_x = ui_scale_px(62), mid_div = ui_scale_px(282), mid_right = ui_scale_px(292);
    label(menu, mid_x, ui_scale_px(20), "Tüm Uygulamalar", INK);
    fill(menu, mid_x, ui_scale_px(42), ui_scale_px(210), 1, UI_LINE);

    static const char *subtitles[] = {
        "Komut Satırı & Shell",
        "Dosyalarım & Disk",
        "Sistem ve Donanım",
        "Jade Web Tarayıcı",
        "Ağ ve İnternet",
        "Video & Müzik Çalar"
    };

    for(int k = 0; k < APP_COUNT; k++) {
        int app = menu_order[k];
        desktop_rect_t r = desktop_app_row_scaled(k, menu_h, (int)ui_scale);
        int is_hov = (hover == 200 + app || (keyboard_menu && menu_choice == app));
        if(is_hov) {
            fill(menu, r.x, r.y, r.w, r.h, 0xffe2e8f0u);
            fill(menu, r.x, r.y, 3, r.h, BLUE);
        } else {
            fill(menu, r.x, r.y, r.w, r.h, 0x00000000u);
        }
        icon(menu, r.x + ui_scale_px(8), r.y + (r.h - ui_scale_px(32)) / 2, app, ui_scale_px(32));
        fit_label(menu, (desktop_rect_t){r.x + ui_scale_px(48), r.y, r.w - ui_scale_px(48), r.h}, 0, ui_scale_px(7), names[app], INK);
        fit_label(menu, (desktop_rect_t){r.x + ui_scale_px(48), r.y, r.w - ui_scale_px(48), r.h}, 0, ui_scale_px(25), subtitles[app], DIM);
    }

    /* Vertical divider between middle and right column */
    fill(menu, mid_div, ui_scale_px(12), 1, menu_h - ui_scale_px(72), UI_LINE);

    /* Right column: Windows 10 Live Tiles */
    label(menu, mid_right, ui_scale_px(20), "Başlangıç", INK);
    label(menu, menu_w - ui_scale_px(140), ui_scale_px(20), "AXIS OS", BLUE);
    fill(menu, mid_right, ui_scale_px(42), menu_w - mid_right - ui_scale_px(16), 1, UI_LINE);

    static const uint32_t tile_colors[] = {
        0xff334155u, /* Terminal */
        0xff0284c7u, /* Dosyalar */
        0xff0f766eu, /* Sistem */
        0xff2563ebu, /* Jade Web */
        0xff059669u, /* Ağ */
        0xff7c3aedu  /* Medya */
    };
    static const char *tile_subtexts[] = {
        "x86_64 Long Mode",
        "/home ve Disk",
        "DirectVideo 160Hz",
        "HTML5 Ultra Hub",
        "Ethernet Çevrimiçi",
        "160 FPS Video Çalar"
    };

    for(int i = 0; i < APP_COUNT; i++) {
        desktop_rect_t tr = desktop_tile_scaled(i, menu_w, menu_h, (int)ui_scale);
        int is_hov = (hover == 230 + i || (keyboard_menu && menu_choice == i));
        int isc = ui_scale_px(40);
        fill(menu, tr.x, tr.y, tr.w, tr.h, tile_colors[i]);
        if(is_hov) gpu_draw_rect(menu, tr.x, tr.y, tr.w, tr.h, 0xff0f172au, 2);
        else gpu_draw_rect(menu, tr.x, tr.y, tr.w, tr.h, 0x20000000u, 1);
        icon(menu, tr.x + (tr.w - isc) / 2, tr.y + (tr.h - ui_scale_px(60)) / 2, i, isc);
        fit_label(menu, tr, ui_scale_px(10), tr.h - ui_scale_px(36), names[i], 0xffffffffu);
        fit_label(menu, tr, ui_scale_px(10), tr.h - ui_scale_px(18), tile_subtexts[i], 0xe0ffffffu);
    }

    /* Bottom footer bar */
    fill(menu, 56, menu_h - 58, menu_w - 68, 1, UI_LINE);
    {
        int av_s = ui_scale_px(36), av_r = ui_scale_px(18);
        ui_round(menu, 66, menu_h - 48, av_s, av_s, av_r, 0xff0284c7u);
        center(menu, 66, menu_h - 40, av_s, "A", 0xffffffffu);
        label(menu, 110, menu_h - 46, "Ali", INK);
        label(menu, 110, menu_h - 28, "AXIS OS 10 Pro v6.0.2", DIM);
    }

    int px = menu_w - 188;
    int rst_hov = (hover == 210);
    int rst_w = ui_scale_px(176), rst_h = ui_scale_px(40);
    if(rst_hov) fill(menu, px, menu_h - rst_h - ui_scale_px(10), rst_w, rst_h, 0xfffee2e2u);
    else fill(menu, px, menu_h - rst_h - ui_scale_px(10), rst_w, rst_h, 0xfff1f5f9u);
    gpu_draw_rect(menu, px, menu_h - rst_h - ui_scale_px(10), rst_w, rst_h, rst_hov ? UI_DANGER : UI_LINE, 1);
    glyph_power(menu, px + ui_scale_px(10), menu_h - rst_h + ui_scale_px(2), rst_hov ? UI_DANGER : INK);
    ui_text_fit(menu, px + ui_scale_px(38), menu_h - rst_h + ui_scale_px(2), ui_scale_px(130), "Yeniden Başlat", rst_hov ? UI_DANGER : INK, 0);
}

static int month_days(int year,int month) {
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return lengths[month-1]+(month==2 && year%4==0 && (year%100!=0||year%400==0));
}
static void calendar_draw(void) {
    panel(calendar);
    if(!time_valid) { label(calendar,24,28,"Saat bekleniyor...",DIM); return; }
    char text[48]; clock_time_text(&wall_time,text); text[5]=0;
    ui_text(calendar,30,24,text,INK,2);
    clock_date_text(&wall_time,text); label(calendar,32,74,text,DIM);

    static const char *months[]={"Ocak","Şubat","Mart","Nisan","Mayıs","Haziran","Temmuz","Ağustos","Eylül","Ekim","Kasım","Aralık"};
    label(calendar,26,122,months[wall_time.month-1],INK);
    number(calendar,270,122,wall_time.year,"");
    fill(calendar,24,150,296,1,UI_LINE);
    static const char *days[]={"Pt","Sa","Ça","Pe","Cu","Ct","Pa"};
    for(int i=0;i<7;i++) center(calendar,24+i*42,166,38,days[i],DIM);
    int first=(wall_time.weekday-(wall_time.day-1)%7+7)%7;
    for(int d=1;d<=month_days(wall_time.year,wall_time.month);d++) {
        int cell=first+d-1,x=24+cell%7*42,y=196+cell/7*30;
        if(d==wall_time.day) {
            ui_round(calendar,x+4,y-2,30,28,4,0xff0284c7u);
        }
        kitoa(d,text); center(calendar,x,y+3,38,text,d==wall_time.day?0xffffffff:INK);
    }
    fill(calendar,24,386,296,1,UI_LINE);
    label(calendar,28,402,offset_minutes?"Istanbul  ·  UTC+3":"UTC  ·  UTC+0",DIM);
    calendar->opaque = 0;
    calendar->opaque_inset = 0;
}

static void files_draw(window_t *w) {
    gpu_surface_t *s=w->surface;
    if(!file_dir) file_dir=fs_root;
    fill(s,1,TITLE_H,w->w-2,w->h-TITLE_H-1,PANEL);
    fill(s,1,TITLE_H,182,w->h-TITLE_H-1,UI_RAISED);
    fill(s,182,TITLE_H,1,w->h-TITLE_H,LINE);
    label(s,22,68,"HIZLI ERİŞİM",DIM);
    const char *places[]={"Bu bilgisayar","Kişisel","Geçici","Sistem"};
    const char *paths[]={"/","/home","/tmp","/etc"};
    for(int i=0;i<4;i++) {
        int yy=100+i*44,sel=file_dir==fs_lookup(paths[i]);
        if(sel) { fill(s,1,yy,181,36,0xffe2e8f0u); fill(s,1,yy,3,36,BLUE); }
        ui_icon_nobg(s,22,yy+6,6,24);
        label(s,54,yy+10,places[i],sel?BLUE:INK);
    }
    label(s,22,w->h-60,"AXIS DISK",DIM);
    ui_text_fit(s,22,w->h-38,148,ata_drive_present()?"Yerel disk":"Bellek diski",DIM,0);
    fill(s,202,60,34,32,UI_RAISED);
    gpu_draw_rect(s,202,60,34,32,LINE,1);
    center(s,202,68,34,"<",BLUE);
    fill(s,248,60,w->w-274,32,UI_RAISED);
    gpu_draw_rect(s,248,60,w->w-274,32,LINE,1);
    ui_text_fit(s,260,64,w->w-298,file_preview?file_preview->name:(file_dir==fs_root?"Bu bilgisayar":file_dir->name),INK,0);
    label(s,204,108,file_preview?"ÖNİZLEME":"AD",DIM);
    if(!file_preview) { label(s,w->w-224,108,"TÜR",DIM); label(s,w->w-100,108,"BOYUT",DIM); }
    fill(s,202,132,w->w-224,1,LINE);
    if(file_preview) {
        char line[160]; int n=0,yy=150;
        const uint8_t *data=fs_node_data(file_preview);
        for(unsigned i=0;i<file_preview->size && yy<w->h-60;i++) {
            unsigned char c=data[i];
            if(c=='\n'||n>=150) { line[n]=0; ui_text_fit(s,204,yy,w->w-232,line,INK,0); n=0; yy+=24; }
            if(c!='\n') line[n++]=(c>=32&&c<127)?c:'.';
        }
        if(n) { line[n]=0; ui_text_fit(s,204,yy,w->w-232,line,INK,0); }
    } else {
        int visible=(w->h-196)/44;
        file_scroll=clamp(file_scroll,0,file_dir->child_count-visible);
        for(int row=0;row<visible && row+file_scroll<file_dir->child_count;row++) {
            vfs_node_t *node=file_dir->children[row+file_scroll]; int yy=144+row*44;
            if(row%2==0) {
                fill(s,198,yy,w->w-218,40,UI_RAISED);
            }
            ui_icon_nobg(s,208,yy+5,file_icon(node),32);
            ui_text_fit(s,252,yy+13,w->w-490,node->name,INK,0);
            label(s,w->w-224,yy+13,node->type==NODE_DIR?"Klasör":"Dosya",DIM);
            if(node->type!=NODE_DIR) { char size[32]; fmt_size(node->size,size); ui_text_fit(s,w->w-100,yy+13,80,size,DIM,0); }
        }
        if(!file_dir->child_count) label(s,222,170,"Bu klasör boş.",DIM);
    }
    fill(s,183,w->h-42,w->w-184,1,LINE);
    number(s,204,w->h-28,file_dir->child_count," öğe");
    label(s,w->w-250,w->h-28,"Gezinmek için ok tuşları",DIM);
}

static void system_draw(window_t *w) {
    gpu_surface_t *s=w->surface;
    icon(s,30,64,2,48);
    ui_text(s,98,64,"Sistem ve Görüntü Ayarları",INK,1);
    label(s,100,102,"AXIS OS 6.0.2  ·  WDDM 2.0 & DWM Retained UI",DIM);

    char mem[32],display[48],tmp[24]; fmt_size(g_sysinfo.total_memory,mem);
    kitoa(sw,display); kstrcat(display," x "); kitoa(sh,tmp); kstrcat(display,tmp);
    if(sw == 1920 && sh == 1080) kstrcat(display, " (Full HD 1080p)");
    else if(sw == 1024 && sh == 768) kstrcat(display, " (XGA Standart)");

    const char *keys[]={"İşlemci","Bellek","Aktif Ekran","Grafik Sürücüsü","Compositor"};
    const char *values[]={
        get_cpu_brand_str(),
        mem,
        display,
        g_wddm_adapter.bga_available ? "Parallels / Bochs Video (WDDM 2.0)" : "UEFI GOP / VESA Sürücüsü",
        "DWM Retained Compositor (Direct3D)"
    };
    for(int i=0;i<5;i++) {
        int yy=134+i*34;
        label(s,32,yy,keys[i],DIM);
        ui_text_fit(s,190,yy,w->w-224,values[i],INK,0);
        fill(s,32,yy+26,w->w-64,1,LINE);
    }

    /* Çözünürlük ve Pencere Boyutu Adaptasyonu */
    int res_base_y = 314;
    ui_text(s, 32, res_base_y, "Ekran Çözünürlüğü (WDDM / Parallels Adaptasyonu):", INK, 1);
    const char *res_labels[] = {
        "3840 x 2160 (4K UHD)",
        "2560 x 1440 (QHD)",
        "1920 x 1080 (FHD)",
        "1600 x 900 (HD+)",
        "1440 x 900 (16:10)",
        "1280 x 720 (HD)",
        "1024 x 768 (XGA)",
        "800 x 600 (SVGA)"
    };
    const uint32_t res_w[] = { 3840, 2560, 1920, 1600, 1440, 1280, 1024,  800 };
    const uint32_t res_h[] = { 2160, 1440, 1080,  900,  900,  720,  768,  600 };

    for(int r = 0; r < 8; r++) {
        int btn_x = 32 + (r % 4) * 165;
        int btn_y = res_base_y + 24 + (r / 4) * 40;
        int active_res = (sw == (int)res_w[r] && sh == (int)res_h[r]);
        uint32_t btn_bg = active_res ? 0xFF0284D7u : UI_RAISED;
        uint32_t btn_border = active_res ? 0xFF0284D7u : UI_LINE;

        fill(s, btn_x, btn_y, 150, 32, btn_bg);
        gpu_draw_rect(s, btn_x, btn_y, 150, 32, btn_border, 1);
        ui_text_fit(s, btn_x + 10, btn_y + 8, 130, res_labels[r], active_res ? 0xFFFFFFFFu : INK, 0);
    }

    fill(s,28,w->h-76,w->w-56,34,UI_RAISED);
    gpu_draw_rect(s,28,w->h-76,w->w-56,34,UI_LINE,1);
    ui_text_fit(s,42,w->h-66,w->w-84,offset_minutes?"Saat dilimi: İstanbul (UTC+3)  ·  Değiştir":"Saat dilimi: UTC  ·  Değiştir",INK,0);
    ui_text_fit(s,32,w->h-30,w->w-64,"İpucu: F11 tuşu ile 4K UHD ve Full HD 1080p arasında anında geçiş yapabilirsiniz.",DIM,0);
}

static void network_draw(window_t *w) {
    gpu_surface_t *s=w->surface;
    int connected=net_info.gateway!=0;
    icon(s,30,70,4,48);
    ui_text_fit(s,98,70,w->w-132,"Ağ bağlantısı",INK,1);
    const char *state=!net_info.present?"Ağ bağdaştırıcısı bulunamadı":!net_info.enabled?"Bağlantı kapalı":connected?"Bağlandı":"Bağlanıyor...";
    ui_text_fit(s,100,112,w->w-132,state,DIM,0);
    char ip[24],router[24],dns[24];
    net_ip_text(net_info.ip,ip); net_ip_text(net_info.router,router); net_ip_text(net_info.dns,dns);
    const char *keys[]={"Bağdaştırıcı","IPv4 adresi","Ağ geçidi","DNS"};
    const char *values[]={"RTL8139 / NAT",ip,router,dns};
    for(int i=0;i<4;i++) {
        int yy=158+i*44;
        label(s,32,yy,keys[i],DIM); ui_text_fit(s,184,yy,w->w-218,values[i],INK,0);
        fill(s,32,yy+32,w->w-64,1,LINE);
    }
    fill(s,28,w->h-116,w->w-56,42,UI_RAISED);
    gpu_draw_rect(s,28,w->h-116,w->w-56,42,UI_LINE,1);
    center(s,28,w->h-104,w->w-56,net_info.enabled?"Bağlantıyı kes":"Bağlan",INK);
    ui_text_fit(s,32,w->h-54,w->w-64,"İnternet erişimi sanal makinenin ağ bağlantısını kullanır.",DIM,0);
}

#define WIN_CORNER_RADIUS 12

#define SHADOW_PAD_X    24
#define SHADOW_PAD_Y    16
#define SHADOW_EXTRA_W  48
#define SHADOW_EXTRA_H  44
/* 4K ölçeklenmiş ek gölge genişliği */
#define SHADOW_EXTRA_W_S  96
#define SHADOW_EXTRA_H_S  88

static void window_draw_shadow(gpu_surface_t *d, int win_w, int win_h, int active) {
    if (!d || !d->pixels) return;
    d->w = win_w + SHADOW_EXTRA_W;
    d->h = win_h + SHADOW_EXTRA_H;
    for (unsigned y = 0; y < d->h; y++) kmemset(d->pixels + y * d->stride, 0, d->w * 4);

    /* Silky multi-step Gaussian drop shadow with subtle downward light projection */
    int num_steps = 18;
    for (int i = 0; i < num_steps; i++) {
        int sx = i;
        int sy = i * SHADOW_PAD_Y / SHADOW_PAD_X + 2;
        int sw = d->w - i * 2;
        int sh = d->h - i * 2 - 4;
        int rad = 30 - i;
        if (rad < 8) rad = 8;
        if (sw <= 0 || sh <= 0) break;

        /* Smooth quadratic ease-in alpha ramp with soft slate graphite tint */
        uint32_t a = (active ? 3 : 2) + (i * (active ? 4 : 2)) / num_steps;
        rounded(d, sx, sy, sw, sh, rad, (a << 24) | 0x000f172au);
    }
}

static void window_draw(int app) {
    window_t *w=&windows[app]; gpu_surface_t *s=w->surface;
    gpu_surface_move(s,w->x,w->y);
    gpu_surface_show(s,w->open&&(!w->minimized||win_anim[app]));
    if(w->shadow) {
        gpu_surface_move(w->shadow,w->x-SHADOW_PAD_X,w->y-SHADOW_PAD_Y);
        gpu_surface_show(w->shadow,w->open&&!w->minimized&&!w->maximized&&!win_anim[app]);
    }
    if(!s->visible) return;
    int active=focused==app;
    int control_hover=(hover>=300+app*3&&hover<303+app*3)?hover:-1;
    int resized=w->painted_w!=w->w||w->painted_h!=w->h;
    int focus_changed=w->painted_focus!=active;
    if(!resized&&!focus_changed&&w->painted_hover==control_hover&&!w->content_dirty) return;
    s->w=w->w; s->h=w->h;
    int body=resized||w->content_dirty;
    int radius=w->maximized?0:WIN_CORNER_RADIUS;

    if((resized || focus_changed) && w->shadow && !w->maximized) {
        gpu_surface_t *d=w->shadow;
        window_draw_shadow(d, w->w, w->h, active);
        gpu_surface_damage(d,0,0,d->w,d->h);
    }

    if(body) {
        for(int y=0;y<w->h;y++) kmemset(s->pixels+y*s->stride,0,w->w*4);
        ui_round_bottom(s,0,TITLE_H,w->w,w->h-TITLE_H,radius,PANEL);
        if(app==0) fill(s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-2,TERM_BG|0xff000000);
        if(app==1||app==2||app==4)axis_web_draw(app,s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-15);
        if(app==3)browser_draw(s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-15);
        if(app==5) media_draw(s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-15);
        if(radius>0) ui_mask_bottom_corners(s,w->w,w->h,radius);
    }

    /* Title and controls: clear top bar before glass straight-alpha rendering */
    for(int y=0;y<TITLE_H;y++) kmemset(s->pixels+y*s->stride,0,w->w*4);

    /* Glass translucent background with rounded top corners */
    uint32_t glass_bg = active ? 0xeef8fafcu : 0xdeedf2f7u;
    ui_round_top(s,0,0,w->w,TITLE_H,radius,glass_bg);

    /* Specular gloss reflection gradient across the upper curved glass lens */
    int gloss_h = 16;
    for(int y=1;y<gloss_h;y++) {
        uint32_t ga = (active ? 28 : 16) - (y * (active ? 26 : 15) / gloss_h);
        if(ga > 0) {
            uint32_t gloss_col = (ga << 24) | 0x00FFFFFFu;
            int inset = (y < radius) ? (radius - y) : 0;
            int line_w = w->w - inset * 2;
            if(line_w > 0) gpu_fill_rect(s, inset, y, line_w, 1, gloss_col);
        }
    }

    /* Glass border highlights & refined separator lines */
    if(radius > 0) {
        uint32_t top_sheen  = active ? 0x90ffffffu : 0x50ffffffu;
        uint32_t side_sheen = active ? 0x50cbd5e1u : 0x30cbd5e1u;
        uint32_t body_sheen = active ? 0x700284c7u : 0x40cbd5e1u;
        /* Top 1px edge sheen */
        gpu_fill_rect(s, radius, 0, w->w - radius * 2, 1, top_sheen);
        /* Title sides */
        gpu_fill_rect(s, 0, radius, 1, TITLE_H - radius, side_sheen);
        gpu_fill_rect(s, w->w - 1, radius, 1, TITLE_H - radius, side_sheen);
        /* Body sides */
        gpu_fill_rect(s, 0, TITLE_H, 1, w->h - TITLE_H - radius, body_sheen);
        gpu_fill_rect(s, w->w - 1, TITLE_H, 1, w->h - TITLE_H - radius, body_sheen);
        /* Bottom edge */
        gpu_fill_rect(s, radius, w->h - 1, w->w - radius * 2, 1, body_sheen);
        /* Anti-aliased 1px curved corner borders */
        ui_round_border_corners(s, w->w, w->h, radius, top_sheen, body_sheen);
    } else {
        /* Maximized: crisp rectangular border */
        gpu_draw_rect(s, 0, 0, w->w, w->h, active ? 0xff0284c7u : LINE, 1);
    }

    /* 1px glass title bar bottom separator */
    fill(s, 0, TITLE_H - 1, w->w, 1, 0x14000000u);
    fill(s, 0, TITLE_H, w->w, 1, 0x18000000u);

    /* Icon and title text */
    icon(s, 14, 10, app, 24);
    const char *title=app==5?media_title():names[app];
    ui_text_fit(s, 48, 14, w->w - 200, title, active ? 0xff0f172au : 0xff64748bu, 0);

    /* Control buttons: modern pill style */
    for(int action=0;action<3;action++) {
        desktop_rect_t r=desktop_control(w->w,action);
        int hot=control_hover==300+app*3+action;
        if(hot) {
            uint32_t btn_bg = (action == 2) ? 0xe0e11d48u : 0x20000000u;
            ui_round(s, r.x + 3, r.y + 6, r.w - 6, r.h - 12, 6, btn_bg);
        }
        int cx=r.x+r.w/2,cy=TITLE_H/2;
        uint32_t ink=(hot && action==2)?0xffffffffu:(active||hot?0xff0f172au:0xff64748bu);
        if(action==0) fill(s,cx-5,cy,10,1,ink);
        if(action==1) {
            if(w->maximized) {
                gpu_draw_rect(s,cx-2,cy-6,9,9,ink,1);
                fill(s,cx-5,cy-3,9,9,hot?(action==2?0xe11d48:0xe2e8f0):0xf8fafc);
                gpu_draw_rect(s,cx-5,cy-3,9,9,ink,1);
            } else {
                gpu_draw_rect(s,cx-5,cy-5,10,10,ink,1);
            }
        }
        if(action==2) {
            gpu_draw_line(s,cx-5,cy-5,cx+5,cy+5,ink);
            gpu_draw_line(s,cx+5,cy-5,cx-5,cy+5,ink);
        }
    }

    s->opaque=0;
    s->opaque_inset=0;
    s->opaque_x0=0;
    s->opaque_y0=TITLE_H+1;
    s->opaque_x1=w->w;
    s->opaque_y1=w->h-radius;

    w->painted_w=w->w; w->painted_h=w->h; w->painted_focus=active;
    w->painted_hover=control_hover; w->content_dirty=0;
    gpu_surface_damage(s,0,0,w->w,body||focus_changed?w->h:TITLE_H+1);
}

static void sync_windows(void) {
    for(int i=0;i<APP_COUNT;i++) {
        window_t *w=&windows[order[i]];
        w->surface->z=10+i*4;
        if(w->shadow) w->shadow->z=w->surface->z-1;
    }
    window_t *w=&windows[0];
    fb_window(w->x+16,w->y+TITLE_H+16,(w->w-32)/FONT_W*FONT_W,(w->h-TITLE_H-32)/FONT_H*FONT_H,
              w->surface->z+1,w->open&&!w->minimized&&!win_anim[0]);
}

static void animate_window(int app,int direction) {
    if(transition_app>=0) {
        win_anim[transition_app]=0;
        windows[transition_app].surface->visible=windows[transition_app].open&&!windows[transition_app].minimized;
        windows[transition_app].surface->alpha=255;
        gpu_surface_move(windows[transition_app].surface,windows[transition_app].x,windows[transition_app].y);
    }
    transition_app=app; win_anim[app]=direction; win_started[app]=get_uptime_ms();
    sync_windows(); dirty=1;
}

/* Move/fade the cached surface without resampling text every frame. */
static void transition_draw(uint64_t now) {
    if(transition_app<0) return;
    int app=transition_app; window_t *w=&windows[app];
    int t=g_sysinfo.tsc_freq_hz?ui_ease((unsigned)(now-win_started[app]),140):256;
    int opening=win_anim[app]>0, opacity=opening?t:256-t;
    w->surface->visible=1;
    gpu_surface_alpha(w->surface,opacity*255/256);
    gpu_surface_move(w->surface,w->x,w->y+(256-opacity)*16/256);
    if(w->shadow) w->shadow->visible=0;
    if(app==0) fb_window_snapshot(w->surface,16,TITLE_H+16);
    if(t==256) {
        win_anim[app]=0; transition_app=-1;
        w->surface->alpha=255;
        gpu_surface_move(w->surface,w->x,w->y);
        w->surface->visible=w->open&&!w->minimized;
        if(w->shadow) gpu_surface_show(w->shadow,w->open&&!w->minimized&&!w->maximized);
        sync_windows();
    }
}

static void calendar_set(int open) {
    if(calendar_open==open) return;
    calendar_from=calendar_value;
    calendar_open=open; calendar_closing=!open;
    calendar_started=get_uptime_ms(); calendar_anim=1; calendar_paint=1; dirty=1;
}

static void focus_window(int app) {
    int pos=0; while(pos<APP_COUNT && order[pos]!=app) pos++;
    for(int i=pos;i+1<APP_COUNT;i++) order[i]=order[i+1];
    order[APP_COUNT-1]=app; focused=app;
    sync_windows(); dirty=1;
}

static void select_next(void) {
    focused=-1;
    for(int i=APP_COUNT-1;i>=0;i--) {
        int app=order[i];
        if(windows[app].open&&!windows[app].minimized) { focused=app; break; }
    }
    sync_windows(); dirty=1;
}

static void menu_close(void) {
    if(!menu_open&&!menu_anim) return;
    if(menu_open) {
        menu_open=0;
        menu_closing=1;
        menu_anim=1;
        menu_started=get_uptime_ms();
        dirty=1;
    }
}

static void open_app(int app) {
    int new_document=!windows[app].open;
    int was_open=windows[app].open&&!windows[app].minimized;
    windows[app].open=1; windows[app].minimized=0;
    windows[app].content_dirty=1;
    context_close();
    menu_close(); calendar_set(0);
    if(new_document&&app==1)axis_web_open(app,"/ui/files.html");
    else if(new_document&&app==2)axis_web_open(app,"/ui/index.html");
    else if(new_document&&app==4)axis_web_open(app,"/ui/network.html");
    if(!was_open) animate_window(app,1);
    focus_window(app);
}

void ui_open_app(int app) { open_app(app); }
void ui_reboot(void) { for(unsigned i=0;i<100000;i++) if(!(inb(0x64)&2)) { outb(0x64,0xfe); break; } }

static void toggle_maximize(int app) {
    window_t *w=&windows[app];
    if(!w->maximized) {
        w->rx=w->x; w->ry=w->y; w->rw=w->w; w->rh=w->h;
        w->x=0; w->y=0; w->w=sw; w->h=sh-taskbar_h_scaled();
        w->content_dirty=1;
    } else { w->x=w->rx; w->y=w->ry; w->w=w->rw; w->h=w->rh; }
    w->maximized=!w->maximized;
    sync_windows(); dirty=1;
}

static void toggle_start(void) {
    context_close();
    calendar_set(0);
    if(menu_open) {
        menu_close();
    } else {
        menu_open=1;
        menu_closing=0;
        menu_anim=1;
        menu_started=get_uptime_ms();
        search_len=0; search[0]=0; menu_choice=0; keyboard_menu=0;
        menu_paint=1; dirty=1;
    }
}

static int hit_test(int x,int y) {
    /* 0. Desktop context menu (topmost popup) */
    if(context_open&&inside(x,y,context_x,context_y,CONTEXT_W,CONTEXT_H)) {
        int my=y-context_y;
        for(int i=0;i<5;i++) {
            int yy=CONTEXT_FIRST+i*CONTEXT_ITEM_H;
            if(inside(x-context_x,my,10,yy,CONTEXT_W-20,CONTEXT_ITEM_H-2))
                return CONTEXT_REFRESH+i;
        }
        return 805;
    }

    /* 0.1 AI Offcanvas Drawer (topmost right drawer) */
    if(ai_open||ai_anim) {
        int ai_h = ai_hit_test(x, y);
        if(ai_h >= 0) return ai_h;
    }

    if(menu_open&&inside(x,y,menu->x,menu->y,menu_w,menu_h)) {
        int mx=x-menu->x,my=y-menu->y;
        if(mx<48) {
            if(my>=menu_h-48) return 210; /* Power */
            if(my>=menu_h-92) return 202; /* Settings */
            if(my>=menu_h-136) return 201; /* Documents */
            if(my>=menu_h-180) return 243; /* User */
            if(my>=menu_h-224) return 244; /* AI Circle */
            return 211;
        }
        for(int k=0;k<APP_COUNT;k++) {
            int app=menu_order[k];
            desktop_rect_t r=desktop_app_row_scaled(k,menu_h,(int)ui_scale);
            if(desktop_contains(r,mx,my)) return 200+app;
        }
        for(int i=0;i<APP_COUNT;i++)
            if(desktop_contains(desktop_tile_scaled(i,menu_w,menu_h,(int)ui_scale),mx,my)) return 230+i;
        if(inside(mx,my,menu_w-188,menu_h-50,176,40)) return 210;
        return 211;
    }
    if((calendar_open||calendar_anim)&&inside(x,y,calendar->x,calendar->y,calendar->w,calendar->h)) return 220;
    if(y>=sh-taskbar_h_scaled()) {
        int ly=y-(sh-taskbar_h_scaled());
        if(desktop_contains(shell_layout.start,x,ly)) return 100;
        if(desktop_contains(shell_layout.copilot,x,ly)) return 98;
        if(desktop_contains(shell_layout.search,x,ly)) return 99;
        for(int i=0;i<APP_COUNT;i++) if(desktop_contains(shell_layout.apps[i],x,ly)) return 101+i;
        if(desktop_contains(shell_layout.peek,x,ly)) return 111;
        if(desktop_contains(shell_layout.clock,x,ly)) return 110;
        if(desktop_contains(shell_layout.network,x,ly)) return 112;
        return 115;
    }

    /* 4. Windows */
    for(int i=APP_COUNT-1;i>=0;i--) {
        int app=order[i]; window_t *w=&windows[app];
        if(!w->open||w->minimized||win_anim[app]||!inside(x,y,w->x,w->y,w->w,w->h)) continue;
        int lx=x-w->x,ly=y-w->y;
        for(int action=0;action<3;action++)
            if(desktop_contains(desktop_control(w->w,action),lx,ly)) return 300+app*3+action;
        if(ly<TITLE_H) return 400+app;
        return 500+app;
    }

    /* 5. Desktop Icons */
    for(int i=0;i<DESKTOP_COUNT;i++)
        if(inside(x,y,desktop_icons->x+4,desktop_icons->y+8+i*104,104,94)) return 600+desktop_apps[i];

    return -1;
}

static void click(int hit,int x,int y) {
    if(hit>=700&&hit<=720) {
        ai_click(hit, x, y);
        dirty=1;
        return;
    }
    if(hit==98||hit==244) {
        ai_toggle();
        if(menu_open) menu_close();
        dirty=1;
        return;
    }
    if(ai_open&&hit!=98&&hit!=244) {
        if(x < sw - AI_DRAWER_WIDTH) {
            ai_close_drawer();
            dirty=1;
        }
    }
    if(context_open) {
        if(hit==CONTEXT_REFRESH) {
            context_close();
            desktop_refresh();
            return;
        }
        if(hit==CONTEXT_WALLPAPER) {
            int count=wallpaper_count();
            if(count>1) wallpaper_set_active((wallpaper_get_active()+1)%count);
            context_close();
            desktop_refresh();
            return;
        }
        if(hit==CONTEXT_DISPLAY) {
            context_close();
            open_app(2);
            return;
        }
        if(hit==CONTEXT_PERSONALIZE) {
            context_close();
            open_app(2);
            return;
        }
        if(hit==CONTEXT_TASKBAR) {
            context_close();
            toggle_start();
            return;
        }
        context_close();
        return;
    }
    if(hit>=230&&hit<230+APP_COUNT) hit=200+hit-230;
    if(hit==100) { toggle_start(); return; }
    if(hit==99) {
        if(!menu_open) toggle_start();
        keyboard_menu=1; menu_paint=1; dirty=1; return;
    }
    if(hit==110) { calendar_set(!calendar_open); menu_close(); return; }
    if(hit==112) { open_app(4); return; }
    if(hit==111) {
        int any=0;
        for(int i=0;i<APP_COUNT;i++) if(windows[i].open&&!windows[i].minimized) any=1;
        for(int i=0;i<APP_COUNT;i++) if(windows[i].open) {
            windows[i].minimized=any; win_anim[i]=0; windows[i].surface->alpha=255;
        }
        transition_app=-1;
        menu_close(); calendar_set(0); select_next(); return;
    }
    if(hit>=101&&hit<101+APP_COUNT) {
        int app=hit-101;
        if(windows[app].open&&!windows[app].minimized&&focused==app) {
            windows[app].minimized=1; animate_window(app,-1); select_next();
        } else {
            open_app(app);
        }
        menu_close(); calendar_set(0); return;
    }
    if(hit>=200&&hit<200+APP_COUNT) {
        int app = hit - 200;
        open_app(app);
        return;
    }
    if(hit==210) {
        for(unsigned i=0;i<100000;i++) if(!(inb(0x64)&2)) { outb(0x64,0xfe); break; }
        return;
    }
    if(hit==211||hit==220||hit==243) return;
    menu_close(); calendar_set(0); dirty=1;
    if(hit>=600&&hit<600+APP_COUNT) {
        int app = hit - 600;
        desktop_selected_mask = (1u << app);
        desktop_icons_draw();
        uint64_t now=get_uptime_ms();
        if(desktop_last_app==app&&now-desktop_last_click<500) {
            open_app(app); desktop_last_app=-1;
        } else { desktop_last_app=app; desktop_last_click=now; }
        return;
    }
    if(hit>=300&&hit<300+APP_COUNT*3) {
        int app=(hit-300)/3,action=(hit-300)%3;
        focus_window(app);
        if(action==0) { windows[app].minimized=1; animate_window(app,-1); select_next(); }
        if(action==1) toggle_maximize(app);
        if(action==2) { win_anim[app]=0; if(transition_app==app) transition_app=-1; windows[app].surface->alpha=255; windows[app].open=0; if(app==5) media_stop(); select_next(); }
    } else if(hit>=400&&hit<400+APP_COUNT) {
        int app=hit-400; focus_window(app);
        if(!windows[app].maximized) { dragging=app; drag_dx=x-windows[app].x; drag_dy=y-windows[app].y; }
    } else if(hit>=500&&hit<500+APP_COUNT) {
        int app=hit-500; focus_window(app);
        window_t *w=&windows[app]; int lx=x-w->x,ly=y-w->y;
        if (app==1||app==2||app==4) {
            char action[256];axis_web_click(app,lx-1,ly-TITLE_H-1,action,sizeof(action));
            if(!kstrcmp(action,"ipc:app:terminal"))open_app(0);
            else if(!kstrcmp(action,"ipc:network:toggle"))net_enable(!net_info.enabled);
            else if(!kstrncmp(action,"https://",8)||!kstrncmp(action,"http://",7))ui_open_browser(action);
            w->content_dirty=1;
            return;
        }
        if(app==1) {
            w->content_dirty=1;
            if(lx<182&&ly>=100&&ly<276) {
                const char *paths[]={"/","/home","/tmp","/etc"};
                vfs_node_t *dir=fs_lookup(paths[(ly-100)/44]);
                if(dir) file_dir=dir;
                file_preview=NULL; file_scroll=0;
            } else if(inside(lx,ly,202,60,34,32)) {
                if(file_preview) file_preview=NULL;
                else if(file_dir&&file_dir->parent) file_dir=file_dir->parent;
                file_scroll=0;
            } else if(lx>=198&&!file_preview&&ly>=144&&ly<w->h-52) {
                int row=(ly-144)/44+file_scroll;
                if(file_dir&&row<file_dir->child_count) {
                    vfs_node_t *node=file_dir->children[row];
                    if(node->type==NODE_DIR) { file_dir=node; file_scroll=0; }
                    else file_preview=node;
                }
            }
        }
        if(app==2) {
            const uint32_t res_w[] = { 3840, 2560, 1920, 1600, 1440, 1280, 1024,  800 };
            const uint32_t res_h[] = { 2160, 1440, 1080,  900,  900,  720,  768,  600 };
            int res_base_y = 314;
            for(int r = 0; r < 8; r++) {
                int btn_x = 32 + (r % 4) * 165;
                int btn_y = res_base_y + 24 + (r / 4) * 40;
                if(inside(lx, ly, btn_x, btn_y, 150, 32)) {
                    wddm_set_mode(res_w[r], res_h[r]);
                    w->content_dirty = 1;
                    return;
                }
            }
            if(inside(lx, ly, 28, w->h-76, w->w-56, 34)) {
                offset_minutes=offset_minutes?0:180; last_second=-1; w->content_dirty=1;
            }
        }
        if(app==3) { browser_click(lx-1,ly-TITLE_H-1); w->content_dirty=1; }
        if(app==5) { media_click(lx-1,ly-TITLE_H-1,w->w-2,w->h-TITLE_H-15); w->content_dirty=1; }
        if(app==4&&inside(lx,ly,28,w->h-116,w->w-56,42)) {
            net_enable(!net_info.enabled); w->content_dirty=1;
        }
    } else {
        focused=-1;
        if(hit == -1) {
            desktop_selected_mask = 0;
            desktop_icons_draw();
            marquee_active = 1;
            marquee_start_x = x;
            marquee_start_y = y;
            marquee_cur_x = x;
            marquee_cur_y = y;
            marquee_old_rx = marquee_old_ry = marquee_old_rw = marquee_old_rh = 0;
        }
    }
}

static inline void dbg_putc(char c) { __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3f8)); }
static void dbg_puts(const char *s) { while (*s) dbg_putc(*s++); }

void ui_init(void) {
    dbg_puts("[UI] ui_init starting\n");
    wallpaper_init();
    dbg_puts("[UI] wallpaper_init done\n");
    sw=gpu_fb_w; sh=gpu_fb_h;
    ui_update_scale(sw);
    int max_w = sw > 1920 ? sw : 1920;
    int max_h = sh > 1080 ? sh : 1080;
    shell_layout=desktop_layout_scaled(sw,sh,256);
    dbg_puts("[UI] 1. creating wallpaper\n");
    wallpaper=gpu_surface_create(max_w,max_h,0,0,0);
    if(wallpaper) { wallpaper->w=sw; wallpaper->h=sh; wallpaper->stride=sw; }
    dbg_puts("[UI] 2. creating desktop_icons\n");
    desktop_icons=gpu_surface_create(112,DESKTOP_COUNT*104+16,16,24,2);
    if(desktop_icons) {
        desktop_icons->opaque=0;
        desktop_icons->visible=1;
    }
    dbg_puts("[UI] 3. creating marquee_surf\n");
    marquee_surf=gpu_surface_create(max_w,max_h,0,0,90);
    if(marquee_surf) {
        kmemset(marquee_surf->pixels,0,max_w*max_h*4);
        marquee_surf->w=sw; marquee_surf->h=sh; marquee_surf->stride=sw;
        marquee_surf->opaque=0;
        marquee_surf->visible=0;
    }
    dbg_puts("[UI] 4. creating taskbar\n");
    {
        int th = ui_scale_px(TASKBAR_H);
        bar=gpu_surface_create(max_w,th,0,sh-th,100);
        if(bar) {
            bar->w=sw; bar->stride=sw;
            bar->opaque=0;
            bar->opaque_inset=0;
            bar->visible=1;
        }
    }
    menu_w=shell_layout.menu_w; menu_h=shell_layout.menu_h;
    menu_x=shell_layout.menu_x; menu_y=shell_layout.menu_y;
    menu=gpu_surface_create(menu_w,menu_h,menu_x,menu_y,110);
    context=gpu_surface_create(CONTEXT_W,CONTEXT_H,8,8,120);
    calendar_x = sw - 344 - 12;
    if(calendar_x < 12) calendar_x = 12;
    calendar_y = sh-taskbar_h_scaled() - 444 - 8;
    calendar=gpu_surface_create(344,444,calendar_x,calendar_y,112);
    menu->visible=calendar->visible=context->visible=0;
    menu->opaque=calendar->opaque=0;
    menu->opaque_inset=calendar->opaque_inset=0;
    context->opaque=0; context->opaque_inset=0;

    ai_init();
    {
        int ai_w = ui_scale_px(AI_DRAWER_WIDTH);
        ai_surface=gpu_surface_create(ai_w,max_h-taskbar_h_scaled(),sw,0,115);
        if(ai_surface) {
            ai_surface->w=ai_w;
            ai_surface->h=sh-taskbar_h_scaled();
            ai_surface->stride=ai_w;
            ai_surface->visible=0;
            ai_surface->opaque=0;
        }
    }
    dbg_puts("[UI] 5. creating window surfaces\n");
    const int widths[]={900,900,720,1040,720,1000},heights[]={570,600,540,680,540,660};
    for(int i=0;i<APP_COUNT;i++) {
        dbg_puts("[UI] window surface create\n");
        window_t *w=&windows[i];
        int wi=ui_scale_px(widths[i]), hi=ui_scale_px(heights[i]);
        w->w=wi<sw-40?wi:sw-40;
        w->h=hi<sh-taskbar_h_scaled()-40?hi:sh-taskbar_h_scaled()-40;
        w->x=clamp((sw-w->w)/2+i*26,0,sw-w->w);
        w->y=clamp((sh-taskbar_h_scaled()-w->h)/2+i*18,0,sh-taskbar_h_scaled()-w->h);
        w->surface=gpu_surface_create(max_w,max_h-taskbar_h_scaled(),0,0,10+i*4);
        w->shadow=gpu_surface_create(max_w+SHADOW_EXTRA_W_S,max_h-taskbar_h_scaled()+SHADOW_EXTRA_H_S,0,0,9+i*4);
        if(w->shadow) { w->shadow->opaque=0; w->shadow->visible=0; }
        if(w->surface) {
            w->surface->w=w->w; w->surface->h=w->h;
            w->surface->opaque=0;
            w->surface->opaque_inset=0;
            w->surface->opaque_x0=0;
            w->surface->opaque_y0=TITLE_H+1;
            w->surface->opaque_x1=w->w;
            w->surface->opaque_y1=w->h-WIN_CORNER_RADIUS;
            w->surface->visible=0;
        }
    }
    dbg_puts("[UI] surfaces created\n");
    file_dir=fs_root;
    browser_init();
    fb_clear(TERM_BG);
    dbg_puts("[UI] wallpaper_draw starting\n");
    wallpaper_draw();
    dbg_puts("[UI] wallpaper_draw done\n");
    desktop_icons_draw();
    gpu_wallpaper_bake();
    taskbar_draw();
    sync_windows(); dirty=1;
    dbg_puts("[UI] calling initial ui_poll\n");
    ui_poll();
    dbg_puts("[UI] ui_init complete\n");
}

void ui_flush(void) {
    if(!wallpaper) return;
    uint64_t now=get_uptime_ms();
    for(int i=0;i<APP_COUNT;i++) window_draw(i);
    transition_draw(now);
    if(menu_anim) {
        uint64_t elapsed=now-menu_started;
        int t=g_sysinfo.tsc_freq_hz?ui_ease(elapsed,160):256;
        if(!menu_closing) {
            menu->alpha=(t*255)/256;
            menu->y=menu_y+((256-t)*16)/256;
        } else {
            menu->alpha=((256-t)*255)/256;
            menu->y=menu_y+(t*12)/256;
        }
        if(t>=256||!g_sysinfo.tsc_freq_hz) {
            menu_anim=0;
            if(menu_closing) {
                menu_closing=0;
                menu->visible=0;
                menu->alpha=255;
                menu->y=menu_y;
            } else {
                menu->alpha=255;
                menu->y=menu_y;
            }
        }
    }
    if(!menu_open&&!menu_anim) {
        menu->alpha=255; menu->x=menu_x; menu->y=menu_y;
    }
    ai_poll(now);
    if(ai_anim&&ai_surface) {
        uint64_t elapsed=now-ai_started;
        int t=g_sysinfo.tsc_freq_hz?ui_ease(elapsed,250):256;
        if(!ai_closing) {
            ai_surface->visible=1;
            ai_surface->x=sw-(AI_DRAWER_WIDTH*t)/256;
        } else {
            ai_surface->x=(sw-AI_DRAWER_WIDTH)+(AI_DRAWER_WIDTH*t)/256;
        }
        if(t>=256||!g_sysinfo.tsc_freq_hz) {
            ai_anim=0;
            if(ai_closing) {
                ai_closing=0;
                ai_open=0;
                ai_surface->visible=0;
                ai_surface->x=sw;
            } else {
                ai_surface->x=sw-AI_DRAWER_WIDTH;
            }
        }
        dirty=1;
    }
    if((ai_open||ai_anim)&&ai_surface&&ai_surface->visible) {
        ai_draw(ai_surface,now);
        gpu_surface_damage(ai_surface,0,0,ai_surface->w,ai_surface->h);
    }
    if(calendar_anim) {
        int t=g_sysinfo.tsc_freq_hz?ui_ease(now-calendar_started,220):256;
        int target=calendar_open?255:0;
        calendar_value=calendar_from+(target-calendar_from)*t/256;
        calendar->alpha=calendar_value;
        calendar->y=calendar_y+(255-calendar_value)*12/255;
        if(t==256) calendar_anim=0;
    }
    uint64_t signature=(unsigned)(focused+1)|(menu_open<<3)|(calendar_open<<4);
    for(int i=0;i<APP_COUNT;i++) signature|=((uint64_t)(windows[i].open|(windows[i].minimized<<1)))<<(5+i*2);
    int bar_hover=hover>=99&&hover<=115?hover:0;
    signature|=((uint64_t)bar_hover)<<18;
    signature|=((uint64_t)(last_network_signature&0xf))<<30;
    if(time_valid) {
        signature|=((uint64_t)wall_time.minute)<<34;
        signature|=((uint64_t)wall_time.hour)<<40;
    }
    if(signature!=last_bar_signature||dock_dirty) {
        taskbar_draw();
        last_bar_signature=signature;
    }
    gpu_surface_show(menu,menu_open||menu_anim);
    gpu_surface_show(calendar,calendar_open||calendar_anim);
    gpu_surface_show(context,context_open);
    if((menu_open||menu_anim)&&menu_paint) { menu_draw(); gpu_surface_damage(menu,0,0,menu_w,menu_h); menu_paint=0; }
    if((calendar_open||calendar_anim)&&calendar_paint) {
        calendar_draw(); gpu_surface_damage(calendar,0,0,calendar->w,calendar->h); calendar_paint=0;
    }
    if(context_open&&context_paint) {
        context_paint=0;
        context_draw();
        gpu_surface_damage(context,0,0,CONTEXT_W,CONTEXT_H);
    }
    dirty=0; gpu_compose();
}

void ui_desktop_draw(void) {
    wallpaper_draw();
    desktop_icons_draw();
    gpu_wallpaper_bake();
    dirty=1;
}

void ui_on_display_resize(uint32_t new_w, uint32_t new_h) {
    if(new_w < 640 || new_h < 480) return;
    sw = (int)new_w;
    sh = (int)new_h;
    ui_update_scale(sw);
    shell_layout = desktop_layout_scaled(sw, sh, 256);

    /* Duvar kağıdı ve arka plan */
    if(wallpaper) {
        wallpaper->w = sw;
        wallpaper->h = sh;
        wallpaper->stride = sw;
        wallpaper_draw();
        desktop_icons_draw();
        gpu_wallpaper_bake();
    }

    int max_w = sw > 1920 ? sw : 1920;
    /* Görev çubuğu — 4K'ta ölçeklenmiş yükseklikte */
    {
        int th = taskbar_h_scaled();
        if(bar && bar->h != (uint32_t)th) {
            gpu_surface_destroy(bar);
            bar = gpu_surface_create(max_w, th, 0, sh - th, 100);
            if(bar) {
                bar->w = sw; bar->stride = sw;
                bar->opaque = 0; bar->opaque_inset = 0; bar->visible = 1;
            }
        }
        if(bar) {
            bar->w = sw; bar->stride = sw;
            bar->x = 0;
            bar->y = sh - th;
            bar->visible = 1;
            taskbar_draw();
        }
    }

    /* Başlat menüsü */
    menu_w = shell_layout.menu_w;
    menu_h = shell_layout.menu_h;
    menu_x = shell_layout.menu_x;
    menu_y = shell_layout.menu_y;
    if(menu) {
        menu->w = menu_w;
        menu->h = menu_h;
        menu->x = menu_x;
        menu->y = menu_y;
        menu->stride = menu_w;
        menu_paint = 1;
    }

    /* Takvim / Saat Flyout */
    calendar_x = sw - 344 - 12;
    if(calendar_x < 12) calendar_x = 12;
    calendar_y = sh-taskbar_h_scaled() - 444 - 8;
    if(calendar) {
        calendar->x = calendar_x;
        calendar->y = calendar_y;
        calendar_paint = 1;
    }

    /* Marquee seçim yüzeyi */
    if(marquee_surf) {
        marquee_surf->w = sw;
        marquee_surf->h = sh;
        marquee_surf->stride = sw;
    }

    /* AI Offcanvas Yüzeyi */
    if(ai_surface) {
        ai_surface->h = sh-taskbar_h_scaled();
        ai_surface->y = 0;
        ai_surface->x = ai_open ? (sw - AI_DRAWER_WIDTH) : sw;
    }

    /* Pencerelerin boyut ve konumlarını yeni ekrana adapte et */
    for(int i = 0; i < APP_COUNT; i++) {
        window_t *win = &windows[i];
        if(win->maximized) {
            win->x = 0;
            win->y = 0;
            win->w = sw;
            win->h = sh-taskbar_h_scaled();
        } else {
            if(win->w > sw - 40) win->w = sw - 40;
            if(win->h > sh-taskbar_h_scaled() - 40) win->h = sh-taskbar_h_scaled() - 40;
            win->x = clamp(win->x, 0, sw - win->w);
            win->y = clamp(win->y, 0, sh-taskbar_h_scaled() - win->h);
        }
        if(win->surface) {
            win->surface->w = win->w;
            win->surface->h = win->h;
            win->surface->x = win->x;
            win->surface->y = win->y;
        }
        win->content_dirty = 1;
        win->painted_w = 0;
        win->painted_h = 0;
        window_draw(i);
    }

    /* Terminal viewport */
    fb_set_viewport(0, 0, sw, sh-taskbar_h_scaled());

    dirty = 1;
}

const char *ui_take_command(void) { return NULL; }

void ui_open_browser(const char *url) { open_app(3); if(url&&*url) browser_navigate(url); }
void ui_open_media(void) {
    open_app(5);
    if(!media_video_frames()) media_bieber();
}
void ui_toggle_maximize(int app) {
    if(app >= 0 && app < APP_COUNT) toggle_maximize(app);
}
int ui_terminal_focused(void) {
    return focused==0&&windows[0].open&&!windows[0].minimized&&!menu_open&&!calendar_open&&!context_open;
}
int ui_handle_key(char key) {
    if(key==KBD_F11) {
        if(sw==1920) wddm_set_mode(1024, 768);
        else wddm_set_mode(1920, 1080);
        return 1;
    }
    if(key==KBD_F12) {
        wddm_cycle_mode();
        return 1;
    }
    if(key==KBD_START) { toggle_start(); return 1; }
    if(key==KBD_ESCAPE) {
        if(ai_open) { ai_close_drawer(); dirty=1; return 1; }
        if(context_open) { context_close(); return 1; }
        if(menu_open||calendar_open) { menu_close(); calendar_set(0); dirty=1; return 1; }
    }
    if(ai_open) {
        ai_key(key);
        dirty=1;
        return 1;
    }
    if(context_open) {
        if(key==KBD_DOWN||key==KBD_UP) {
            int step=key==KBD_DOWN?1:-1;
            context_hover=context_hover<CONTEXT_REFRESH?
                (step>0?CONTEXT_REFRESH:CONTEXT_TASKBAR):
                CONTEXT_REFRESH+(context_hover-CONTEXT_REFRESH+5+step)%5;
            context_paint=1; dirty=1;
        } else if(key=='\n'&&context_hover>=CONTEXT_REFRESH) click(context_hover,0,0);
        return 1;
    }
    if(focused==3&&windows[3].open&&!windows[3].minimized&&!menu_open&&!calendar_open) {
        browser_key(key); windows[3].content_dirty=1; dirty=1; return 1;
    }
    if((focused==1||focused==2||focused==4)&&!menu_open&&!calendar_open&&axis_web_key(focused,key)) {
        windows[focused].content_dirty=1;dirty=1;return 1;
    }
    if(menu_open) {
        keyboard_menu=1;
        if(key=='\n') { int app=menu_choice>=0&&matches(menu_choice)?menu_choice:first_match(); if(app>=0) open_app(app); }
        else if(key=='\b') { if(search_len) { do { search_len--; } while(search_len&&((uint8_t)search[search_len]&0xc0)==0x80); search[search_len]=0; } menu_choice=first_match(); }
        else if(key==KBD_RIGHT||key==KBD_DOWN||key=='\t'||key==KBD_LEFT||key==KBD_UP) {
            int step=(key==KBD_LEFT||key==KBD_UP)?-1:1;
            for(int i=0;i<APP_COUNT;i++) { menu_choice=(menu_choice+APP_COUNT+step)%APP_COUNT; if(matches(menu_choice)) break; }
        } else if((uint8_t)key>=32&&key!=127&&search_len<(int)sizeof(search)-1) {
            search[search_len++]=key; search[search_len]=0; menu_choice=first_match();
        }
        menu_paint=1; dirty=1; return 1;
    }
    if(focused==1&&(key==KBD_DOWN||key==KBD_UP)) {
        file_scroll+=key==KBD_DOWN?1:-1; windows[1].content_dirty=1; dirty=1; return 1;
    }
    if(focused==5) { media_key(key); windows[5].content_dirty=1; dirty=1; return 1; }
    return !ui_terminal_focused();
}

void ui_poll(void) {
    mouse_poll();
    if(!wallpaper) return;
    net_poll();
    if(browser_poll()) { windows[3].content_dirty=1; dirty=1; }
    unsigned web_changes=axis_web_poll();
    for(int app=1;app<=4;app++)if(web_changes&(1u<<app)){windows[app].content_dirty=1;dirty=1;}
    if(media_poll()) { windows[5].content_dirty=1; dirty=1; }
    unsigned network_signature=net_info.present|(net_info.enabled<<1)|(net_info.link<<2)|(net_info.gateway<<3);
    if(network_signature!=last_network_signature) {
        last_network_signature=network_signature; windows[4].content_dirty=1; dirty=1;
    }
    int hit=hit_test(g_mouse.x,g_mouse.y);
    if(hit!=hover) {
        if((hit>=200&&hit<=245)||(hover>=200&&hover<=245)) { menu_paint=1; keyboard_menu=0; dirty=1; }
        int redraw_icons=(hit>=600&&hit<600+APP_COUNT)||(hover>=600&&hover<600+APP_COUNT);
        hover=hit;
        if(context_open) {
            int next_context_hover=(hit>=CONTEXT_REFRESH&&hit<=CONTEXT_TASKBAR)?hit:-1;
            if(next_context_hover!=context_hover) {
                context_hover=next_context_hover; context_paint=1;
                dirty=1;
            }
        }
        if(redraw_icons) desktop_icons_draw(); dirty=1;
    }
    uint8_t pressed=g_mouse.pressed; g_mouse.pressed=0;
    if(pressed&2) {
        context_close();
        int right_hit=hit_test(g_mouse.press_x,g_mouse.press_y);
        if(right_hit==-1||(right_hit>=600&&right_hit<600+APP_COUNT))
            context_open_at(g_mouse.press_x,g_mouse.press_y);
    }
    if(pressed&1) click(hit_test(g_mouse.press_x,g_mouse.press_y),g_mouse.press_x,g_mouse.press_y);
    if(windows[5].open&&!windows[5].minimized&&!win_anim[5]) {
        window_t *m=&windows[5];
        int mx=g_mouse.x-m->x-1,my=g_mouse.y-m->y-TITLE_H-1;
        if(focused!=5 || menu_open || calendar_open || context_open) { mx=-1; my=-1; }
        if(media_pointer(mx,my,(g_mouse.buttons&1)&&focused==5,m->w-2,m->h-TITLE_H-15)) {
            m->content_dirty=1; dirty=1;
        }
    }
    if(dragging>=0) {
        window_t *w=&windows[dragging];
        int x=clamp(g_mouse.x-drag_dx,0,sw-w->w),y=clamp(g_mouse.y-drag_dy,0,sh-taskbar_h_scaled()-w->h);
        if(x!=w->x||y!=w->y) {
            w->x=x; w->y=y;
            gpu_surface_move(w->surface, x, y);
            if(w->shadow) gpu_surface_move(w->shadow, x-SHADOW_PAD_X, y-SHADOW_PAD_Y);
            if(dragging==0) {
                fb_window(x+16,y+TITLE_H+16,(w->w-32)/FONT_W*FONT_W,(w->h-TITLE_H-32)/FONT_H*FONT_H,
                          w->surface->z+1,w->open&&!w->minimized&&!win_anim[0]);
            }
            dirty=1;
        }
        if(!(g_mouse.buttons&1)) dragging=-1;
    }
    if(marquee_active) {
        if(g_mouse.buttons & 1) {
            marquee_cur_x = g_mouse.x;
            marquee_cur_y = g_mouse.y;
            int rx = marquee_start_x < marquee_cur_x ? marquee_start_x : marquee_cur_x;
            int ry = marquee_start_y < marquee_cur_y ? marquee_start_y : marquee_cur_y;
            int rw = (marquee_cur_x > marquee_start_x ? marquee_cur_x - marquee_start_x : marquee_start_x - marquee_cur_x);
            int rh = (marquee_cur_y > marquee_start_y ? marquee_cur_y - marquee_start_y : marquee_start_y - marquee_cur_y);
            if(marquee_surf) {
                if(marquee_old_rw > 0 && marquee_old_rh > 0) {
                    int my0 = marquee_old_ry < 0 ? 0 : marquee_old_ry;
                    int my1 = marquee_old_ry + marquee_old_rh > sh ? sh : marquee_old_ry + marquee_old_rh;
                    int mx0 = marquee_old_rx < 0 ? 0 : marquee_old_rx;
                    int mx1 = marquee_old_rx + marquee_old_rw > sw ? sw : marquee_old_rx + marquee_old_rw;
                    int mw = mx1 - mx0;
                    if(mw > 0) {
                        for(int my = my0; my < my1; my++) {
                            kmemset(&marquee_surf->pixels[my * marquee_surf->stride + mx0], 0, mw * 4);
                        }
                    }
                    gpu_surface_damage(marquee_surf, mx0, my0, mw, my1 - my0);
                }
                if(rw > 2 && rh > 2) {
                    uint32_t fill_color = 0x2238bdf8u;
                    int py0 = ry < 0 ? 0 : ry;
                    int py1 = ry + rh > sh ? sh : ry + rh;
                    int px0 = rx < 0 ? 0 : rx;
                    int px1 = rx + rw > sw ? sw : rx + rw;
                    int pw = px1 - px0;
                    if(pw > 0) {
                        for(int py = py0; py < py1; py++) {
                            uint32_t *line = &marquee_surf->pixels[py * marquee_surf->stride + px0];
                            for(int pxi = 0; pxi < pw; pxi++) {
                                line[pxi] = fill_color;
                            }
                        }
                    }
                    gpu_draw_rect(marquee_surf, rx, ry, rw, rh, 0xaa38bdf8u, 1);
                    gpu_surface_damage(marquee_surf, px0, py0, pw, py1 - py0);
                    marquee_surf->visible = 1;
                    marquee_old_rx = rx; marquee_old_ry = ry;
                    marquee_old_rw = rw; marquee_old_rh = rh;

                    uint32_t new_mask = 0;
                    for(int i = 0; i < DESKTOP_COUNT; i++) {
                        int ix=desktop_icons->x+4, iy=desktop_icons->y+8+i*104, iw=104, ih=94;
                        if(rx < ix + iw && rx + rw > ix && ry < iy + ih && ry + rh > iy) {
                            new_mask |= (1u << desktop_apps[i]);
                        }
                    }
                    if(new_mask != desktop_selected_mask) {
                        desktop_selected_mask = new_mask;
                        desktop_icons_draw();
                    }
                }
                dirty = 1;
            }
        } else {
            marquee_active = 0;
            if(marquee_surf) {
                if(marquee_old_rw > 0 && marquee_old_rh > 0) {
                    int my0 = marquee_old_ry < 0 ? 0 : marquee_old_ry;
                    int my1 = marquee_old_ry + marquee_old_rh > sh ? sh : marquee_old_ry + marquee_old_rh;
                    int mx0 = marquee_old_rx < 0 ? 0 : marquee_old_rx;
                    int mx1 = marquee_old_rx + marquee_old_rw > sw ? sw : marquee_old_rx + marquee_old_rw;
                    int mw = mx1 - mx0;
                    if(mw > 0) {
                        for(int my = my0; my < my1; my++) {
                            kmemset(&marquee_surf->pixels[my * marquee_surf->stride + mx0], 0, mw * 4);
                        }
                    }
                    gpu_surface_damage(marquee_surf, mx0, my0, mw, my1 - my0);
                }
                marquee_surf->visible = 0;
            }
            marquee_old_rw = marquee_old_rh = 0;
            dirty = 1;
        }
    }
    uint64_t now=get_uptime_ms();
    if(last_second<0 || (g_sysinfo.tsc_freq_hz>=1000 ? now>=next_clock : ++clock_poll>=4096)) {
        clock_poll=0; next_clock=now+100; time_valid=clock_read(&wall_time,offset_minutes);
        if(time_valid&&last_second!=wall_time.second) {
            last_second=wall_time.second;
            if(wall_time.second==0||last_bar_signature==(uint64_t)-1) taskbar_clock_draw();
            dirty=1;
            if(calendar_open) calendar_paint=1;
        }
    }
    int animating=menu_anim||calendar_anim;
    for(int i=0;i<APP_COUNT;i++) animating|=win_anim[i];
    if(animating && (now-last_animation>=6 || !g_sysinfo.tsc_freq_hz)) {
        last_animation=now; dirty=1;
    }
    if(dirty && (animating || dragging>=0 || now>=next_ui_frame || !g_sysinfo.tsc_freq_hz)) { next_ui_frame=now+6; ui_flush(); }
    else gpu_compose();
}
