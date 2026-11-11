/* SHENLONG desktop. Cooperative window manager; no applications open at boot. */
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

#define BAR_H 56
#define TITLE_H 42
#define INK UI_TEXT
#define DIM UI_MUTED
#define BLUE UI_ACCENT
#define PANEL UI_PANEL
#define LINE UI_LINE
#define APP_COUNT 5

typedef struct {
    int open, minimized, maximized, x,y,w,h,rx,ry,rw,rh;
    int painted_w, painted_h, painted_focus, painted_hover, content_dirty;
    gpu_surface_t *surface;
} window_t;
static window_t windows[APP_COUNT];
static const char *names[]={"Terminal","Dosyalar","Sistem","Web","Ağ"};
static const char *descriptions[]={"Komutlar ve geliştirme","Dosyaların ve klasörlerin","Görünüm ve sistem bilgisi","Hafif HTTP tarayıcı","Host internet bağlantısı"};
static int order[APP_COUNT]={0,1,2,3,4}, focused=-1;
static gpu_surface_t *wallpaper,*bar,*menu,*calendar;
static int sw,sh,start_x,launcher_x,menu_x,menu_y,menu_w,menu_h;
static int menu_open,calendar_open,dirty,dragging=-1,drag_dx,drag_dy;
static int offset_minutes=180,hover=-1,menu_choice;

/* Animasyon state */
#define ANIM_MS 180
static int menu_anim=0;      /* Time-based opening animation is active. */
static int win_anim[APP_COUNT]; /* pencere acilma animasyonu */
static uint64_t menu_started, win_started[APP_COUNT], last_animation, next_clock;
static int menu_paint=1, calendar_paint=1;
static uint64_t last_bar_signature=(uint64_t)-1;
static unsigned last_network_signature=(unsigned)-1;
static char search[48];
static int search_len;
static wall_time_t wall_time;
static int last_second=-1,time_valid;
static unsigned clock_poll;
static vfs_node_t *file_dir,*file_preview;

static int inside(int px,int py,int x,int y,int w,int h) {
    return px>=x && py>=y && px<x+w && py<y+h;
}
static int clamp(int n,int a,int b) { if(b<a) b=a; return n<a?a:(n>b?b:n); }
static void fill(gpu_surface_t *s,int x,int y,int w,int h,uint32_t c) {
    if(w>0&&h>0) gpu_fill_rect(s,x,y,w,h,c);
}
static void rounded(gpu_surface_t *s,int x,int y,int w,int h,int r,uint32_t c) {
    if(w<=0||h<=0) return;
    if(r>w/2) r=w/2; if(r>h/2) r=h/2;
    fill(s,x+r,y,w-2*r,h,c);
    fill(s,x,y+r,r,h-2*r,c); fill(s,x+w-r,y+r,r,h-2*r,c);
    for(int py=0;py<r;py++) for(int px=0;px<r;px++) {
        unsigned coverage=0;
        for(int sy=0;sy<4;sy++) for(int sx=0;sx<4;sx++) {
            int dx=(r-px)*8-1-sx*2,dy=(r-py)*8-1-sy*2;
            coverage+=dx*dx+dy*dy<=r*r*64;
        }
        if(!coverage) continue;
        uint32_t color=(c&0xffffff)|((GPU_A(c)*coverage/16)<<24);
        gpu_put_pixel(s,x+px,y+py,color); gpu_put_pixel(s,x+w-1-px,y+py,color);
        gpu_put_pixel(s,x+px,y+h-1-py,color); gpu_put_pixel(s,x+w-1-px,y+h-1-py,color);
    }
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
static void panel(gpu_surface_t *s) {
    s->opaque=1; s->opaque_inset=0;
    s->opaque_x0=s->opaque_y0=s->opaque_x1=s->opaque_y1=0;
    gpu_fill(s,PANEL);
    gpu_draw_rect(s,0,0,s->w,s->h,LINE,1);
}
static void translucent(gpu_surface_t *s,int height) {
    /* Only chrome is translucent; text and app content retain full contrast. */
    if(height>(int)s->h-1) height=s->h-1;
    for(int y=1;y<height;y++) for(unsigned x=1;x+1<s->w;x++)
        s->pixels[y*s->stride+x]=(PANEL&0xffffff)|0xf0000000u;
    s->opaque=0; s->opaque_inset=height==(int)s->h-1?0:height;
    if(height<(int)s->h-1) {
        s->opaque_x0=1; s->opaque_y0=height;
        s->opaque_x1=s->w-1; s->opaque_y1=s->h-1;
    }
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
    gpu_fill(wallpaper,UI_BG);
    for(int i=0;i<APP_COUNT;i++) {
        icon(wallpaper,26,28+i*112,i,64);
        center(wallpaper,10,98+i*112,96,names[i],0xffb5bec4u);
    }
}

static void taskbar_clock_draw(void) {
    fill(bar,sw-260,0,260,BAR_H,PANEL);
    fill(bar,sw-260,0,260,1,LINE);
    icon(bar,sw-252,16,4,24);
    rounded(bar,sw-222,25,6,6,3,net_info.gateway&&net_info.enabled?BLUE:DIM);
    if(hover==110||calendar_open) rounded(bar,sw-174,4,154,48,5,UI_HOVER);
    char text[32];
    if(time_valid) {
        clock_time_text(&wall_time,text); center(bar,sw-170,4,140,text,INK);
        clock_date_text(&wall_time,text); center(bar,sw-170,27,140,text,DIM);
    } else center(bar,sw-170,17,140,"Saat alınamadı",DIM);
    fill(bar,sw-10,12,1,32,LINE);
    gpu_surface_damage(bar,sw-260,0,260,BAR_H);
}
static void taskbar_draw(void) {
    gpu_fill(bar,PANEL);
    fill(bar,0,0,sw,1,LINE);
    if(menu_open||hover==100) fill(bar,launcher_x,5,164,46,UI_HOVER);
    icon(bar,launcher_x+8,10,-1,32); label(bar,launcher_x+48,15,"SHENLONG",menu_open?INK:DIM);
    fill(bar,launcher_x+172,16,1,24,LINE);
    for(int i=0;i<APP_COUNT;i++) {
        int x=start_x+i*52;
        int active=windows[i].open&&!windows[i].minimized&&focused==i;
        if(active||hover==101+i) rounded(bar,x,5,44,46,8,active?UI_HOVER:UI_RAISED);
        icon(bar,x+6,10,i,32);
        if(windows[i].open)
            rounded(bar,x+(active?12:18),49,active?20:8,3,1,active?BLUE:DIM);
    }
    taskbar_clock_draw();
    gpu_surface_damage(bar,0,0,sw,BAR_H);
}

static void menu_draw(void) {
    panel(menu);
    translucent(menu,menu->h);
    icon(menu,24,22,-1,40);
    ui_text(menu,80,14,"Uygulamalar",INK,1);
    label(menu,82,50,"SHENLONG",DIM);
    rounded(menu,24,86,menu_w-48,42,8,UI_BG);
    label(menu,42,96,search_len?search:"Uygulama bul...",search_len?INK:DIM);
    fill(menu,34,126,menu_w-68,1,search_len?BLUE:LINE);
    int count=0;
    for(int i=0;i<APP_COUNT;i++) {
        if(!matches(i)) continue;
        int y=146+count*66;
        if(hover==200+i||menu_choice==i) rounded(menu,20,y,menu_w-40,60,8,UI_HOVER);
        icon(menu,34,y+10,i,40);
        label(menu,92,y+6,names[i],INK);
        label(menu,92,y+31,descriptions[i],DIM);
        label(menu,menu_w-48,y+20,">",DIM);
        count++;
    }
    if(!count) label(menu,36,170,"Eşleşen uygulama bulunamadı.",DIM);
    fill(menu,24,menu_h-65,menu_w-48,1,LINE);
    label(menu,30,menu_h-43,"Yerel oturum",DIM);
    rounded(menu,menu_w-174,menu_h-52,146,36,6,hover==210?UI_HOVER:UI_RAISED);
    center(menu,menu_w-174,menu_h-46,146,"Yeniden başlat",INK);
}

static int month_days(int year,int month) {
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return lengths[month-1]+(month==2 && year%4==0 && (year%100!=0||year%400==0));
}
static void calendar_draw(void) {
    panel(calendar);
    translucent(calendar,calendar->h);
    char text[32];
    if(!time_valid) { label(calendar,24,24,"Sistem saati okunamadı.",INK); return; }
    clock_time_text(&wall_time,text); ui_text(calendar,24,18,text,INK,2);
    clock_date_text(&wall_time,text); label(calendar,28,78,text,DIM);
    static const char *months[]={"Ocak","Şubat","Mart","Nisan","Mayıs","Haziran",
        "Temmuz","Ağustos","Eylül","Ekim","Kasım","Aralık"};
    ui_text(calendar,26,116,months[wall_time.month-1],INK,1);
    number(calendar,250,125,wall_time.year,"");
    static const char *days[]={"Pt","Sa","Ça","Pe","Cu","Ct","Pa"};
    for(int i=0;i<7;i++) center(calendar,24+i*44,164,40,days[i],DIM);
    int first=(wall_time.weekday-(wall_time.day-1)%7+7)%7;
    for(int d=1;d<=month_days(wall_time.year,wall_time.month);d++) {
        int cell=first+d-1,x=24+(cell%7)*44,y=196+(cell/7)*32;
        if(d==wall_time.day) rounded(calendar,x+4,y,30,30,15,BLUE);
        kitoa(d,text); center(calendar,x,y+3,40,text,d==wall_time.day?0xffffffff:INK);
    }
    fill(calendar,24,394,312,1,LINE);
    label(calendar,26,407,offset_minutes?"İstanbul / UTC+3":"UTC / UTC+0",DIM);
}

static void files_draw(window_t *w) {
    gpu_surface_t *s=w->surface;
    fill(s,1,TITLE_H,w->w-2,w->h-TITLE_H-1,PANEL);
    fill(s,1,TITLE_H,158,w->h-TITLE_H-1,UI_RAISED);
    label(s,20,64,"<  Üst klasör",BLUE);
    label(s,20,114,"Bu bilgisayar",INK);
    label(s,20,160,"Ana dizin",DIM);
    label(s,20,202,"Geçici dosyalar",DIM);
    label(s,20,244,"Sistem dosyaları",DIM);
    label(s,182,58,file_preview?file_preview->name:(file_dir==fs_root?"/":"Klasör"),INK);
    if(file_dir!=fs_root&&!file_preview) label(s,260,58,file_dir->name,DIM);
    fill(s,178,92,w->w-196,1,LINE);
    if(file_preview) {
        label(s,184,108,"Dosya önizlemesi",DIM);
        char line[96]; int n=0,y=146;
        for(unsigned i=0;i<file_preview->size && y<w->h-70;i++) {
            unsigned char c=file_preview->data[i];
            if(c=='\n'||n>=70) { line[n]=0; label(s,184,y,line,INK); n=0; y+=24; }
            if(c!='\n') line[n++]=(c>=32&&c<127)?c:'.';
        }
        if(n) { line[n]=0; label(s,184,y,line,INK); }
    } else {
        label(s,190,102,"Ad",DIM); label(s,w->w-180,102,"Tür",DIM);
        for(int i=0;i<file_dir->child_count && 140+i*36<w->h-48;i++) {
            vfs_node_t *node=file_dir->children[i];
            int y=132+i*36;
            if(i%2==0) rounded(s,178,y,w->w-194,34,4,UI_RAISED);
            icon(s,188,y+5,node->type==NODE_DIR?1:0,22);
            label(s,224,y+5,node->name,INK);
            label(s,w->w-180,y+5,node->type==NODE_DIR?"Klasör":"Dosya",DIM);
        }
        if(!file_dir->child_count) label(s,190,154,"Bu klasör boş.",DIM);
    }
    fill(s,160,w->h-38,w->w-161,1,LINE);
    label(s,180,w->h-30,"RAM diski  /  Bu oturum",DIM);
}

static void system_draw(window_t *w) {
    gpu_surface_t *s=w->surface;
    ui_text(s,28,66,"Sistem hakkında",INK,1);
    label(s,30,107,"SHENLONG OS 0.6  /  Night Studio",DIM);
    const char *keys[]={"İşlemci","Bellek","Önyükleme","Ekran","Arayüz yazısı"};
    char mem[32],display[48],tmp[24]; fmt_size(g_sysinfo.total_memory,mem);
    kitoa(sw,display); kstrcat(display," x "); kitoa(sh,tmp); kstrcat(display,tmp);
    const char *values[]={get_cpu_brand_str(),mem,g_sysinfo.firmware_type?"UEFI / Limine":"BIOS / Limine",display,"Shenlong UI / Selawik tabanlı"};
    for(int i=0;i<5;i++) {
        int y=156+i*48;
        fill(s,28,y+36,w->w-56,1,LINE);
        label(s,30,y,keys[i],DIM); label(s,176,y,values[i],INK);
    }
    rounded(s,28,w->h-100,w->w-56,40,6,UI_HOVER);
    label(s,42,w->h-91,offset_minutes?"Saat dilimi: İstanbul (UTC+3)  ·  Değiştir":"Saat dilimi: UTC (UTC+0)  ·  Değiştir",BLUE);
    label(s,30,w->h-46,"Super: Uygulamalar   /   Esc: Menüyü kapat",DIM);
}

static void network_draw(window_t *w) {
    gpu_surface_t *s=w->surface;
    icon(s,28,72,4,56);
    ui_text(s,106,70,"Host bağlantısı",INK,1);
    const char *state=!net_info.present?"Sanal ağ kartı bulunamadı":!net_info.enabled?"Bağlantı kapalı":
        !net_info.link?"Sanal kablo bağlı değil":net_info.gateway?"Host ağı hazır":"Host ağı aranıyor...";
    label(s,108,108,state,net_info.gateway?BLUE:DIM);
    label(s,30,168,"Hostun Wi-Fi veya Ethernet bağlantısı paylaşılır.",INK);
    label(s,30,198,"Wi-Fi ağını macOS üzerinden seç; burada şifre gerekmez.",DIM);
    fill(s,28,240,w->w-56,1,LINE);
    char ip[24],router[24],dns[24];
    net_ip_text(net_info.ip,ip); net_ip_text(net_info.router,router); net_ip_text(net_info.dns,dns);
    const char *keys[]={"Bağlantı","IPv4 adresi","Ağ geçidi","DNS"};
    const char *values[]={"Host paylaşımı / NAT",ip,router,dns};
    for(int i=0;i<4;i++) { label(s,30,260+i*32,keys[i],DIM); label(s,220,260+i*32,values[i],INK); }
    rounded(s,28,w->h-116,w->w-56,42,7,UI_HOVER);
    center(s,28,w->h-106,w->w-56,net_info.enabled?"Bağlantıyı kes":"Bağlan",net_info.present?BLUE:DIM);
    label(s,30,w->h-52,"İnternet erişimi host bağlantısına bağlıdır.",DIM);
}

static void window_draw(int app) {
    window_t *w=&windows[app]; gpu_surface_t *s=w->surface;
    gpu_surface_move(s,w->x,w->y);
    gpu_surface_show(s,w->open&&!w->minimized);
    if(!s->visible) return;
    int active=focused==app;
    int control_hover=(hover>=300+app*3&&hover<303+app*3)?hover:-1;
    if(w->painted_w==w->w && w->painted_h==w->h &&
       w->painted_focus==active && w->painted_hover==control_hover && !w->content_dirty) return;
    s->w=w->w; s->h=w->h; s->stride=w->w;
    int body=w->painted_w!=w->w || w->painted_h!=w->h || w->content_dirty;
    if(body) panel(s);
    else fill(s,1,1,w->w-2,TITLE_H-1,PANEL);
    translucent(s,TITLE_H);
    if(focused==app) fill(s,1,1,w->w-2,1,0xff515b62u);
    icon(s,14,12,app,20); label(s,46,9,names[app],INK);
    for(int i=0;i<3;i++) {
        int x=w->w-138+i*46;
        if(hover==300+app*3+i) rounded(s,x,1,45,40,5,i==2?0xff713a40:UI_HOVER);
        uint32_t col=(hover==300+app*3+i&&i==2)?0xffffffff:INK;
        if(i==0) fill(s,x+18,24,12,1,col);
        if(i==1) gpu_draw_rect(s,x+18,15,12,11,col,1);
        if(i==2) {
            gpu_draw_line(s,x+18,15,x+28,25,col);
            gpu_draw_line(s,x+28,15,x+18,25,col);
        }
    }
    fill(s,1,TITLE_H,w->w-2,1,LINE);
    if(body) {
        if(app==0) fill(s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-2,TERM_BG|0xff000000);
        if(app==1) files_draw(w);
        if(app==2) system_draw(w);
        if(app==3) browser_draw(s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-2);
        if(app==4) network_draw(w);
    }
    w->painted_w=w->w; w->painted_h=w->h;
    w->painted_focus=active; w->painted_hover=control_hover; w->content_dirty=0;
    gpu_surface_damage(s,0,0,w->w,body?w->h:TITLE_H+1);
}

static void sync_windows(void) {
    for(int i=0;i<APP_COUNT;i++) windows[order[i]].surface->z=10+i*4;
    window_t *w=&windows[0];
    fb_window(w->x+16,w->y+TITLE_H+16,(w->w-32)/FONT_W*FONT_W,(w->h-TITLE_H-32)/FONT_H*FONT_H,
              w->surface->z+1,w->open&&!w->minimized);
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
static void open_app(int app) {
    int was_open=windows[app].open&&!windows[app].minimized;
    windows[app].open=1; windows[app].minimized=0;
    windows[app].content_dirty=1;
    menu_open=calendar_open=0; menu_anim=0;
    if(!was_open) { win_anim[app]=1; win_started[app]=get_uptime_ms(); }
    focus_window(app);
}
static void toggle_maximize(int app) {
    window_t *w=&windows[app];
    if(!w->maximized) {
        w->rx=w->x; w->ry=w->y; w->rw=w->w; w->rh=w->h;
        w->x=0; w->y=0; w->w=sw; w->h=sh-BAR_H;
    } else { w->x=w->rx; w->y=w->ry; w->w=w->rw; w->h=w->rh; }
    w->maximized=!w->maximized;
    sync_windows(); dirty=1;
}
static void toggle_start(void) {
    menu_open=!menu_open; calendar_open=0;
    search_len=0; search[0]=0; menu_choice=0;
    menu_anim=menu_open?1:0; menu_started=get_uptime_ms(); menu_paint=1; dirty=1;
}

static int hit_test(int x,int y) {
    if(y>=sh-BAR_H) {
        if(inside(x,y,launcher_x,sh-BAR_H+4,164,48)) return 100;
        for(int i=0;i<APP_COUNT;i++) if(inside(x,y,start_x+i*52,sh-BAR_H+4,44,48)) return 101+i;
        if(x>=sw-260&&x<sw-200) return 112;
        if(x>=sw-174&&x<sw-12) return 110;
        if(x>=sw-12) return 111;
        return -1;
    }
    if(menu_open && inside(x,y,menu->x,menu->y,menu_w,menu_h)) {
        x-=menu->x; y-=menu->y;
        int row=0;
        for(int i=0;i<APP_COUNT;i++) if(matches(i)) {
            if(inside(x,y,20,146+row*66,menu_w-40,60)) return 200+i;
            row++;
        }
        if(inside(x,y,menu_w-174,menu_h-52,146,36)) return 210;
        return 211;
    }
    if(calendar_open && inside(x,y,calendar->x,calendar->y,calendar->w,calendar->h)) return 220;
    for(int i=APP_COUNT-1;i>=0;i--) {
        int app=order[i]; window_t *w=&windows[app];
        if(!w->open||w->minimized||!inside(x,y,w->x,w->y,w->w,w->h)) continue;
        int lx=x-w->x,ly=y-w->y;
        if(ly<TITLE_H && lx>=w->w-138) return 300+app*3+(lx-(w->w-138))/46;
        if(ly<TITLE_H) return 400+app;
        return 500+app;
    }
    for(int i=0;i<APP_COUNT;i++) if(inside(x,y,12,24+i*112,94,104)) return 600+i;
    return -1;
}

static void click(int hit,int x,int y) {
    if(hit==100) { toggle_start(); return; }
    if(hit==110) { calendar_open=!calendar_open; calendar_paint=1; menu_open=0; dirty=1; return; }
    if(hit==112) { open_app(4); return; }
    if(hit==111) {
        int any=0;
        for(int i=0;i<APP_COUNT;i++) if(windows[i].open&&!windows[i].minimized) any=1;
        for(int i=0;i<APP_COUNT;i++) if(windows[i].open) windows[i].minimized=any;
        menu_open=calendar_open=0; select_next(); return;
    }
    if(hit>=101&&hit<101+APP_COUNT) {
        int app=hit-101;
        if(windows[app].open&&!windows[app].minimized&&focused==app) {
            windows[app].minimized=1; select_next();
        } else open_app(app);
        menu_open=calendar_open=0; return;
    }
    if(hit>=200&&hit<200+APP_COUNT) { open_app(hit-200); return; }
    if(hit==210) {
        for(unsigned i=0;i<100000;i++) if(!(inb(0x64)&2)) { outb(0x64,0xfe); break; }
        return;
    }
    if(hit==211||hit==220) return;
    menu_open=calendar_open=0; dirty=1;
    if(hit>=600&&hit<600+APP_COUNT) { open_app(hit-600); return; }
    if(hit>=300&&hit<300+APP_COUNT*3) {
        int app=(hit-300)/3,action=(hit-300)%3;
        focus_window(app);
        if(action==0) { windows[app].minimized=1; select_next(); }
        if(action==1) toggle_maximize(app);
        if(action==2) { windows[app].open=0; select_next(); }
    } else if(hit>=400&&hit<400+APP_COUNT) {
        int app=hit-400; focus_window(app);
        if(!windows[app].maximized) { dragging=app; drag_dx=x-windows[app].x; drag_dy=y-windows[app].y; }
    } else if(hit>=500&&hit<500+APP_COUNT) {
        int app=hit-500; focus_window(app);
        window_t *w=&windows[app]; int lx=x-w->x,ly=y-w->y;
        if(app==1) {
            w->content_dirty=1;
            if(lx<160) {
                if(ly<100) { if(file_preview) file_preview=NULL; else file_dir=file_dir->parent; }
                else { file_preview=NULL;
                    if(ly<146) file_dir=fs_root;
                    else if(ly<188) file_dir=fs_lookup("/home");
                    else if(ly<230) file_dir=fs_lookup("/tmp");
                    else file_dir=fs_lookup("/etc");
                }
            } else if(!file_preview && ly>=132 && ly<w->h-48) {
                int row=(ly-132)/36;
                if(row<file_dir->child_count) {
                    vfs_node_t *node=file_dir->children[row];
                    if(node->type==NODE_DIR) file_dir=node; else file_preview=node;
                }
            }
        }
        if(app==2&&inside(lx,ly,28,w->h-100,w->w-56,40)) {
            offset_minutes=offset_minutes?0:180; last_second=-1; w->content_dirty=1;
        }
        if(app==3) { browser_click(lx-1,ly-TITLE_H-1); w->content_dirty=1; }
        if(app==4&&inside(lx,ly,28,w->h-116,w->w-56,42)) {
            net_enable(!net_info.enabled); w->content_dirty=1;
        }
    } else { focused=-1; }
}

void ui_init(void) {
    sw=gpu_fb_w; sh=gpu_fb_h; launcher_x=sw>=1100?(sw-444)/2:12; start_x=launcher_x+184;
    wallpaper=gpu_surface_create(sw,sh,0,0,0);
    bar=gpu_surface_create(sw,BAR_H,0,sh-BAR_H,100);
    menu_w=480; menu_h=556;
    if(menu_h>sh-BAR_H-24) menu_h=sh-BAR_H-24;
    menu_x=(sw-menu_w)/2; menu_y=sh-BAR_H-menu_h-12;
    menu=gpu_surface_create(menu_w,menu_h,menu_x,menu_y,110);
    calendar=gpu_surface_create(360,444,sw-376,sh-BAR_H-456,112);
    menu->visible=calendar->visible=0;
    menu->opaque=calendar->opaque=0;
    const int widths[]={900,820,720,1040,760},heights[]={570,560,530,680,540};
    for(int i=0;i<APP_COUNT;i++) {
        window_t *w=&windows[i];
        w->w=widths[i]<sw-40?widths[i]:sw-40;
        w->h=heights[i]<sh-BAR_H-40?heights[i]:sh-BAR_H-40;
        w->x=clamp((sw-w->w)/2+i*26,0,sw-w->w);
        w->y=clamp((sh-BAR_H-w->h)/2+i*18,0,sh-BAR_H-w->h);
        /* Reserve once. Close/reopen/resize never leaks the bump allocator. */
        w->surface=gpu_surface_create(sw,sh,0,0,10+i*4);
        w->surface->visible=0;
    }
    file_dir=fs_root;
    browser_init();
    fb_clear(TERM_BG);
    wallpaper_draw();
    gpu_wallpaper_bake(); /* The wallpaper surface remains cached between frames. */
    sync_windows(); dirty=1;
    ui_poll();
}

static int ease_open(uint64_t elapsed) {
    if(!g_sysinfo.tsc_freq_hz||elapsed>=ANIM_MS) return 255;
    unsigned remaining=255-elapsed*255/ANIM_MS;
    return 255-remaining*remaining*remaining/(255*255);
}
void ui_flush(void) {
    if(!wallpaper) return;
    uint64_t now=get_uptime_ms();
    for(int i=0;i<APP_COUNT;i++) {
        if(win_anim[i]) {
            uint64_t elapsed=now-win_started[i];
            int a=ease_open(elapsed);
            windows[i].surface->alpha=a;
            if(a==255) win_anim[i]=0;
        }
        window_draw(i); /* cached contents: dragging changes only geometry */
    }
    fb_window_alpha(windows[0].surface->alpha);
    if(menu_anim&&menu_open) {
        uint64_t elapsed=now-menu_started;
        int t=ease_open(elapsed);
        menu->y=menu_y+(255-t)*14/255; menu->alpha=t;
        if(t==255) menu_anim=0;
    }
    if(!menu_open) { menu_anim=0; menu->alpha=255; menu->y=menu_y; }
    uint64_t signature=(unsigned)(focused+1)|(menu_open<<3)|(calendar_open<<4);
    for(int i=0;i<APP_COUNT;i++) signature|=(windows[i].open|(windows[i].minimized<<1))<<(5+i*2);
    signature|=(uint64_t)((hover>=100&&hover<=112)?hover-99:0)<<20;
    signature|=(uint64_t)last_network_signature<<28;
    if(signature!=last_bar_signature) { taskbar_draw(); last_bar_signature=signature; }
    gpu_surface_show(menu,menu_open);
    gpu_surface_show(calendar,calendar_open);
    if(menu_open&&menu_paint) {
        menu_draw(); gpu_surface_damage(menu,0,0,menu_w,menu_h); menu_paint=0;
    }
    if(calendar_open&&calendar_paint) {
        calendar_draw(); gpu_surface_damage(calendar,0,0,calendar->w,calendar->h); calendar_paint=0;
    }
    dirty=0;
    gpu_compose();
}
void ui_desktop_draw(void) { wallpaper_draw(); gpu_wallpaper_bake(); dirty=1; }
const char *ui_take_command(void) { return NULL; }
void ui_open_browser(const char *url) { open_app(3); if(url&&*url) browser_navigate(url); }
int ui_terminal_focused(void) {
    return focused==0&&windows[0].open&&!windows[0].minimized&&!menu_open&&!calendar_open;
}
int ui_handle_key(char key) {
    if(key==KBD_START) { toggle_start(); return 1; }
    if(focused==3&&windows[3].open&&!windows[3].minimized&&!menu_open&&!calendar_open) {
        browser_key(key); windows[3].content_dirty=1; dirty=1; return 1;
    }
    if(key==KBD_ESCAPE) { menu_open=calendar_open=0; dirty=1; return 1; }
    if(menu_open) {
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
    return !ui_terminal_focused();
}
void ui_poll(void) {
    mouse_poll();
    if(!wallpaper) return;
    net_poll();
    if(browser_poll()) { windows[3].content_dirty=1; dirty=1; }
    unsigned network_signature=net_info.present|(net_info.enabled<<1)|(net_info.link<<2)|(net_info.gateway<<3);
    if(network_signature!=last_network_signature) {
        last_network_signature=network_signature; windows[4].content_dirty=1; dirty=1;
    }
    int hit=hit_test(g_mouse.x,g_mouse.y);
    if(hit!=hover) {
        if((hit>=200&&hit<=211)||(hover>=200&&hover<=211)) menu_paint=1;
        hover=hit; dirty=1;
    }
    uint8_t pressed=g_mouse.pressed; g_mouse.pressed=0;
    if(pressed&1) click(hit_test(g_mouse.press_x,g_mouse.press_y),g_mouse.press_x,g_mouse.press_y);
    if(dragging>=0) {
        window_t *w=&windows[dragging];
        int x=clamp(g_mouse.x-drag_dx,0,sw-w->w),y=clamp(g_mouse.y-drag_dy,0,sh-BAR_H-w->h);
        if(x!=w->x||y!=w->y) { w->x=x; w->y=y; sync_windows(); dirty=1; }
        if(!(g_mouse.buttons&1)) dragging=-1;
    }
    uint64_t now=get_uptime_ms();
    if(last_second<0 || (g_sysinfo.tsc_freq_hz>=1000 ? now>=next_clock : ++clock_poll>=4096)) {
        clock_poll=0; next_clock=now+100; time_valid=clock_read(&wall_time,offset_minutes);
        if(time_valid&&last_second!=wall_time.second) {
            last_second=wall_time.second;
            taskbar_clock_draw();
            if(calendar_open) { calendar_paint=1; dirty=1; }
        }
    }
    int animating=menu_anim;
    for(int i=0;i<APP_COUNT;i++) animating|=win_anim[i];
    if(animating && (now-last_animation>=8 || !g_sysinfo.tsc_freq_hz)) {
        last_animation=now; dirty=1;
    }
    if(dirty) ui_flush();
    else gpu_compose(); /* pointer-only damage and paced tail frames */
}
