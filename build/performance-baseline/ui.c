/* SHENLONG desktop. Cooperative window manager; no applications open at boot. */
#include "ui.h"
#include "ui_font.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "kernel_info.h"
#include "clock.h"
#include "fs.h"
#include "logo.h"

#define BAR_H 56
#define TITLE_H 42
#define INK 0xff203047u
#define DIM 0xff64748bu
#define BLUE 0xff1769d2u
#define PANEL 0xfff6f8fcu
#define LINE 0xffdce3eeu
#define APP_COUNT 3

typedef struct {
    int open, minimized, maximized, x,y,w,h,rx,ry,rw,rh;
    gpu_surface_t *surface;
} window_t;
static window_t windows[APP_COUNT];
static const char *names[]={"Terminal","Dosyalar","Sistem"};
static int order[APP_COUNT]={0,1,2}, focused=-1;
static gpu_surface_t *wallpaper,*bar,*menu,*calendar;
static int sw,sh,start_x,menu_x,menu_y,menu_w,menu_h;
static int menu_open,calendar_open,dirty,dragging=-1,drag_dx,drag_dy;
static int offset_minutes=180,hover=-1,menu_choice;

/* Animasyon state */
#define ANIM_STEPS 8
static int menu_anim=0;      /* 0=kapali, 1..ANIM_STEPS=aciyor, ANIM_STEPS=tam acik */
static int win_anim[APP_COUNT]; /* pencere acilma animasyonu */
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
    for(int py=0;py<h;py++) {
        int dy=py<r?r-1-py:(py>=h-r?py-(h-r):0), inset=0;
        if(dy) { int dx=r; while(dx*dx+dy*dy>r*r) dx--; inset=r-dx; }
        fill(s,x+inset,y+py,w-2*inset,1,c);
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
    if(app==0) {
        rounded(s,x,y,size,size,6,0xff1e293bu);
        gpu_draw_line(s,x+size/4,y+size/3,x+size/2,y+size/2,0xffe2e8f0);
        gpu_draw_line(s,x+size/2,y+size/2,x+size/4,y+size*2/3,0xffe2e8f0);
        fill(s,x+size/2,y+size*2/3,size/4,2,0xff67dbff);
    } else if(app==1) {
        rounded(s,x,y+size/6,size/2,size/3,3,0xffd99318);
        rounded(s,x,y+size/3,size,size*2/3,4,0xfff5bb42);
        fill(s,x+2,y+size/3+2,size-4,3,0xffffdc84);
    } else if(app==2) {
        rounded(s,x,y,size,size,7,0xffc4d4e8);
        rounded(s,x+size/5,y+size/5,size*3/5,size*3/5,4,BLUE);
        rounded(s,x+size/3,y+size/3,size/3,size/3,3,0xffeef5ff);
    } else {
        int half=(size-3)/2;
        for(int i=0;i<4;i++) fill(s,x+(i%2)*(half+3),y+(i/2)*(half+3),half,half,BLUE);
    }
}
static void clear_surface(gpu_surface_t *s) { kmemset(s->pixels,0,(size_t)s->w*s->h*4); }
static void panel(gpu_surface_t *s) {
    clear_surface(s);
    rounded(s,0,0,s->w,s->h,12,0xffb8c9df);
    rounded(s,1,1,s->w-2,s->h-2,11,PANEL);
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
    /* Koyu mor-siyah gradient arkaplan */
    for(int y=0;y<sh;y++) {
        int t=y*255/sh;
        unsigned r=(uint8_t)(4  + t*8/255);
        unsigned g=(uint8_t)(2  + t*4/255);
        unsigned b=(uint8_t)(18 + t*20/255);
        uint32_t col=GPU_RGB(r,g,b);
        for(int x=0;x<sw;x++) wallpaper->pixels[y*sw+x]=col;
    }

    /* Ejderha logosu — tam ekrana scale et, ortala, 55% opasite */
    int lw=sw*85/100, lh=(int)((uint64_t)lw*LOGO_H/LOGO_W);
    if(lh>sh*85/100) { lh=sh*85/100; lw=(int)((uint64_t)lh*LOGO_W/LOGO_H); }
    int lx=(sw-lw)/2, ly=(sh-lh)/2 - BAR_H/2;
    if(ly<0) ly=0;
    for(int dy=0;dy<lh;dy++) {
        int sy=(int)((uint64_t)dy*LOGO_H/lh); if(sy>=(int)LOGO_H) sy=LOGO_H-1;
        for(int dx=0;dx<lw;dx++) {
            int sx=(int)((uint64_t)dx*LOGO_W/lw); if(sx>=(int)LOGO_W) sx=LOGO_W-1;
            int idx=(sy*(int)LOGO_W+sx)*3;
            unsigned lr=logo_rgb[idx], lg=logo_rgb[idx+1], lb=logo_rgb[idx+2];
            if(!lr&&!lg&&!lb) continue; /* tamamen siyah → seffaf */
            /* 55% opasite blend */
            int px=lx+dx, py=ly+dy;
            if(px<0||py<0||px>=sw||py>=sh) continue;
            uint32_t bg=wallpaper->pixels[py*sw+px];
            unsigned br=GPU_R(bg),bg2=GPU_G(bg),bb=GPU_B(bg);
            unsigned nr=(lr*55+br*45)/100;
            unsigned ng=(lg*55+bg2*45)/100;
            unsigned nb=(lb*55+bb*45)/100;
            wallpaper->pixels[py*sw+px]=GPU_RGB(nr,ng,nb);
        }
    }

    /* Alt sol: OS adı ve slogan */
    label(wallpaper,32,sh-BAR_H-56,"SHENLONG OS",0xffcc88ff);
    label(wallpaper,32,sh-BAR_H-32,"v0.3  ·  x86_64  ·  GPU Desktop",0xff664488);

    /* Sol dock ikonları */
    for(int i=0;i<APP_COUNT;i++) {
        icon(wallpaper,22,32+i*100,i,48);
        center(wallpaper,8,86+i*100,80,names[i],0xffddccff);
    }
}

static void taskbar_draw(void) {
    gpu_fill(bar,0xffe4ecf7);
    fill(bar,0,0,sw,1,0xfff5f8ff);
    label(bar,24,17,"SHENLONG",DIM);
    for(int i=0;i<4;i++) {
        int x=start_x+i*52;
        int active=i==0?menu_open:(windows[i-1].open&&!windows[i-1].minimized&&focused==i-1);
        if(active||hover==100+i) rounded(bar,x,5,44,46,6,active?0xfff7faff:0xffd6e3f4);
        icon(bar,x+10,14,i-1,24);
        if(i && windows[i-1].open)
            rounded(bar,x+(active?12:18),49,active?20:8,3,1,active?BLUE:0xff98a9bc);
    }
    label(bar,sw-218,18,"TR",DIM);
    if(hover==110||calendar_open) rounded(bar,sw-174,4,154,48,5,0xffd6e3f4);
    char text[32];
    if(time_valid) {
        clock_time_text(&wall_time,text); center(bar,sw-170,4,140,text,INK);
        clock_date_text(&wall_time,text); center(bar,sw-170,27,140,text,DIM);
    } else center(bar,sw-170,17,140,"Saat alınamadı",DIM);
    fill(bar,sw-10,12,1,32,0xffa8b8cd);
}

static void menu_draw(void) {
    panel(menu);
    rounded(menu,24,24,menu_w-48,42,7,0xffffffff);
    fill(menu,32,64,menu_w-64,2,BLUE);
    label(menu,42,33,search_len?search:"Uygulama ara...",search_len?INK:DIM);
    label(menu,36,92,"Sabitlenmiş",INK);
    label(menu,menu_w-150,92,"3 uygulama",DIM);
    int count=0;
    for(int i=0;i<APP_COUNT;i++) {
        if(!matches(i)) continue;
        int x=30+i*166;
        if(hover==200+i||menu_choice==i) rounded(menu,x,132,150,104,8,0xffe4edf9);
        icon(menu,x+55,146,i,40);
        center(menu,x,198,150,names[i],INK);
        count++;
    }
    if(!count) label(menu,36,160,"Eşleşen uygulama bulunamadı.",DIM);
    fill(menu,30,266,menu_w-60,1,LINE);
    label(menu,36,290,"Her şey yerli yerinde.",INK);
    label(menu,36,324,"Uygulamaları buradan veya görev çubuğundan aç.",DIM);
    label(menu,36,354,"Pencereleri taşı, küçült ve kaldığın yerden devam et.",DIM);
    rounded(menu,30,404,menu_w-60,48,8,0xffeaf0f8);
    label(menu,46,417,"Dosyalar RAM'de tutulur; yeniden başlatınca silinir.",DIM);
    fill(menu,1,menu_h-68,menu_w-2,1,LINE);
    rounded(menu,30,menu_h-49,30,30,15,0xffc2d6f0);
    center(menu,30,menu_h-46,30,"S",BLUE);
    label(menu,74,menu_h-46,"Yerel kullanıcı",INK);
    rounded(menu,menu_w-174,menu_h-52,146,36,6,hover==210?0xffdce7f5:0xffedf2f9);
    center(menu,menu_w-174,menu_h-46,146,"Yeniden başlat",INK);
}

static int month_days(int year,int month) {
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return lengths[month-1]+(month==2 && year%4==0 && (year%100!=0||year%400==0));
}
static void calendar_draw(void) {
    panel(calendar);
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
    fill(s,1,TITLE_H,w->w-2,w->h-TITLE_H-1,0xffffffff);
    fill(s,1,TITLE_H,158,w->h-TITLE_H-1,0xffeef3f9);
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
            if(i%2==0) rounded(s,178,y,w->w-194,34,4,0xfff5f8fc);
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
    label(s,30,107,"SHENLONG OS 0.4  /  Masaüstü",DIM);
    const char *keys[]={"İşlemci","Bellek","Önyükleme","Ekran","Arayüz yazısı"};
    char mem[32],display[48],tmp[24]; fmt_size(g_sysinfo.total_memory,mem);
    kitoa(sw,display); kstrcat(display," x "); kitoa(sh,tmp); kstrcat(display,tmp);
    const char *values[]={get_cpu_brand_str(),mem,g_sysinfo.firmware_type?"UEFI / Limine":"BIOS / Limine",display,"Shenlong UI / Selawik tabanlı"};
    for(int i=0;i<5;i++) {
        int y=156+i*48;
        fill(s,28,y+36,w->w-56,1,LINE);
        label(s,30,y,keys[i],DIM); label(s,176,y,values[i],INK);
    }
    rounded(s,28,w->h-100,w->w-56,40,6,0xffe4edf9);
    label(s,42,w->h-91,offset_minutes?"Saat dilimi: İstanbul (UTC+3)  ·  Değiştir":"Saat dilimi: UTC (UTC+0)  ·  Değiştir",BLUE);
    label(s,30,w->h-46,"Win: Başlat   /   Esc: Menüyü kapat",DIM);
}

static void window_draw(int app) {
    window_t *w=&windows[app]; gpu_surface_t *s=w->surface;
    s->w=w->w; s->h=w->h; s->stride=w->w;
    panel(s);
    if(focused==app) fill(s,12,1,w->w-24,2,0xff88b9f3);
    icon(s,14,12,app,20); label(s,46,9,names[app],INK);
    for(int i=0;i<3;i++) {
        int x=w->w-138+i*46;
        if(hover==300+app*3+i) rounded(s,x,1,45,40,5,i==2?0xffdd4e55:0xffe0e8f3);
        uint32_t col=(hover==300+app*3+i&&i==2)?0xffffffff:INK;
        if(i==0) fill(s,x+18,24,12,1,col);
        if(i==1) gpu_draw_rect(s,x+18,15,12,11,col,1);
        if(i==2) {
            gpu_draw_line(s,x+18,15,x+28,25,col);
            gpu_draw_line(s,x+28,15,x+18,25,col);
        }
    }
    fill(s,1,TITLE_H,w->w-2,1,LINE);
    if(app==0) fill(s,1,TITLE_H+1,w->w-2,w->h-TITLE_H-2,TERM_BG|0xff000000);
    if(app==1) files_draw(w);
    if(app==2) system_draw(w);
    s->x=w->x; s->y=w->y; s->visible=w->open&&!w->minimized;
}

static void sync_windows(void) {
    for(int i=0;i<APP_COUNT;i++) windows[order[i]].surface->z=10+i*4;
    window_t *w=&windows[0];
    fb_window(w->x+16,w->y+TITLE_H+16,(w->w-32)/8*8,(w->h-TITLE_H-32)/16*16,
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
    menu_open=calendar_open=0; menu_anim=0;
    if(!was_open) win_anim[app]=1;
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
    menu_anim=menu_open?1:0; dirty=1;
}

static int hit_test(int x,int y) {
    if(y>=sh-BAR_H) {
        for(int i=0;i<4;i++) if(inside(x,y,start_x+i*52,sh-BAR_H+4,44,48)) return 100+i;
        if(x>=sw-174&&x<sw-12) return 110;
        if(x>=sw-12) return 111;
        return -1;
    }
    if(menu_open && inside(x,y,menu_x,menu_y,menu_w,menu_h)) {
        x-=menu_x; y-=menu_y;
        for(int i=0;i<APP_COUNT;i++) if(matches(i)&&inside(x,y,30+i*166,132,150,104)) return 200+i;
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
    for(int i=0;i<APP_COUNT;i++) if(inside(x,y,12,24+i*100,94,84)) return 600+i;
    return -1;
}

static void click(int hit,int x,int y) {
    if(hit==100) { toggle_start(); return; }
    if(hit==110) { calendar_open=!calendar_open; menu_open=0; dirty=1; return; }
    if(hit==111) {
        int any=0;
        for(int i=0;i<APP_COUNT;i++) if(windows[i].open&&!windows[i].minimized) any=1;
        for(int i=0;i<APP_COUNT;i++) if(windows[i].open) windows[i].minimized=any;
        menu_open=calendar_open=0; select_next(); return;
    }
    if(hit>=101&&hit<=103) {
        int app=hit-101;
        if(windows[app].open&&!windows[app].minimized&&focused==app) {
            windows[app].minimized=1; select_next();
        } else open_app(app);
        menu_open=calendar_open=0; return;
    }
    if(hit>=200&&hit<203) { open_app(hit-200); return; }
    if(hit==210) {
        for(unsigned i=0;i<100000;i++) if(!(inb(0x64)&2)) { outb(0x64,0xfe); break; }
        return;
    }
    if(hit==211||hit==220) return;
    menu_open=calendar_open=0; dirty=1;
    if(hit>=600&&hit<603) { open_app(hit-600); return; }
    if(hit>=300&&hit<309) {
        int app=(hit-300)/3,action=(hit-300)%3;
        focus_window(app);
        if(action==0) { windows[app].minimized=1; select_next(); }
        if(action==1) toggle_maximize(app);
        if(action==2) { windows[app].open=0; select_next(); }
    } else if(hit>=400&&hit<403) {
        int app=hit-400; focus_window(app);
        if(!windows[app].maximized) { dragging=app; drag_dx=x-windows[app].x; drag_dy=y-windows[app].y; }
    } else if(hit>=500&&hit<503) {
        int app=hit-500; focus_window(app);
        window_t *w=&windows[app]; int lx=x-w->x,ly=y-w->y;
        if(app==1) {
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
            offset_minutes=offset_minutes?0:180; last_second=-1;
        }
    } else { focused=-1; }
}

void ui_init(void) {
    sw=gpu_fb_w; sh=gpu_fb_h; start_x=(sw-208)/2;
    wallpaper=gpu_surface_create(sw,sh,0,0,0);
    bar=gpu_surface_create(sw,BAR_H,0,sh-BAR_H,100);
    menu_w=560; menu_h=536;
    if(menu_h>sh-BAR_H-24) menu_h=sh-BAR_H-24;
    menu_x=(sw-menu_w)/2; menu_y=sh-BAR_H-menu_h-12;
    menu=gpu_surface_create(menu_w,menu_h,menu_x,menu_y,110);
    calendar=gpu_surface_create(360,444,sw-376,sh-BAR_H-456,112);
    const int widths[]={900,820,720},heights[]={570,560,530};
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
    fb_clear(TERM_BG);
    wallpaper_draw();
    gpu_wallpaper_bake(); /* wallpaper snapshot al — compose'da hizli kullanilir */
    sync_windows(); dirty=1;
    ui_poll();
}

void ui_flush(void) {
    if(!wallpaper) return;

    /* Pencere animasyonu: scale+fade pop-in */
    for(int i=0;i<APP_COUNT;i++) {
        if(win_anim[i]>0 && win_anim[i]<=ANIM_STEPS) {
            /* Surface alpha: 0→255 */
            int a=win_anim[i]*255/ANIM_STEPS;
            windows[i].surface->alpha=a;
            win_anim[i]++;
            if(win_anim[i]>ANIM_STEPS) { windows[i].surface->alpha=255; win_anim[i]=0; }
            dirty=1;
        }
    }

    /* Menu animasyonu: slide up + fade */
    if(menu_anim>0 && menu_anim<=ANIM_STEPS) {
        int t=menu_anim*ANIM_STEPS/ANIM_STEPS;
        int slide=(ANIM_STEPS-menu_anim)*40/ANIM_STEPS; /* max 40px kayma */
        menu->y=menu_y+slide;
        menu->alpha=menu_anim*255/ANIM_STEPS;
        menu_anim++;
        if(menu_anim>ANIM_STEPS) { menu->y=menu_y; menu->alpha=255; menu_anim=0; }
        dirty=1; (void)t;
    }

    for(int i=0;i<APP_COUNT;i++) window_draw(i);
    taskbar_draw();
    menu->visible=menu_open||menu_anim>0;
    calendar->visible=calendar_open;
    if(menu_open||menu_anim>0) menu_draw();
    if(calendar_open) calendar_draw();
    gpu_compose(); dirty=0;
}
void ui_desktop_draw(void) { wallpaper_draw(); dirty=1; }
const char *ui_take_command(void) { return NULL; }
int ui_terminal_focused(void) {
    return focused==0&&windows[0].open&&!windows[0].minimized&&!menu_open&&!calendar_open;
}
int ui_handle_key(char key) {
    if(key==KBD_START) { toggle_start(); return 1; }
    if(key==KBD_ESCAPE) { menu_open=calendar_open=0; dirty=1; return 1; }
    if(menu_open) {
        if(key=='\n') { int app=menu_choice>=0&&matches(menu_choice)?menu_choice:first_match(); if(app>=0) open_app(app); }
        else if(key=='\b') { if(search_len) search[--search_len]=0; menu_choice=first_match(); }
        else if(key==KBD_RIGHT||key==KBD_DOWN||key=='\t'||key==KBD_LEFT||key==KBD_UP) {
            int step=(key==KBD_LEFT||key==KBD_UP)?-1:1;
            for(int i=0;i<APP_COUNT;i++) { menu_choice=(menu_choice+APP_COUNT+step)%APP_COUNT; if(matches(menu_choice)) break; }
        } else if(key>=32&&key<=126&&search_len<(int)sizeof(search)-1) {
            search[search_len++]=key; search[search_len]=0; menu_choice=first_match();
        }
        dirty=1; return 1;
    }
    return !ui_terminal_focused();
}
void ui_poll(void) {
    mouse_poll();
    if(!wallpaper) return;

    /* Hover: sadece hit zone degisince dirty yap, kaba mouse hareketi degil */
    int hit=hit_test(g_mouse.x,g_mouse.y);
    if(hit!=hover) { hover=hit; dirty=1; }

    uint8_t pressed=g_mouse.pressed; g_mouse.pressed=0;
    if(pressed&1) {
        click(hit_test(g_mouse.press_x,g_mouse.press_y),
              g_mouse.press_x,g_mouse.press_y);
        dirty=1;
    }

    if(dragging>=0) {
        window_t *w=&windows[dragging];
        int x=clamp(g_mouse.x-drag_dx,0,sw-w->w);
        int y=clamp(g_mouse.y-drag_dy,0,sh-BAR_H-w->h);
        if(x!=w->x||y!=w->y) { w->x=x; w->y=y; sync_windows(); dirty=1; }
        if(!(g_mouse.buttons&1)) dragging=-1;
    }

    /* Saat guncelleme — sadece saniye degisince */
    if(g_sysinfo.tsc_freq_hz>=1000||++clock_poll>=4096||last_second<0) {
        clock_poll=0; time_valid=clock_read(&wall_time,offset_minutes);
        if(time_valid&&last_second!=wall_time.second) {
            last_second=wall_time.second;
            taskbar_draw();
            if(calendar_open) calendar_draw();
            dirty=1;
        }
    }

    /* Compose: gpu_compose kendi icinde TSC throttle yapiyor */
    if(dirty) { ui_flush(); }
}
