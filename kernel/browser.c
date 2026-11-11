#include "browser.h"
#include "web.h"
#include "net.h"
#include "ui_font.h"
#include "theme.h"
#include "ui_paint.h"
#include "keyboard.h"
#include "media.h"
#include "ui.h"
#include "chromium_engine.h"
#include "html_ui.h"
#include "fs.h"
#include "axis_web.h"

static web_page_t page;
static char *body;  /* NET_HTTP_CAP+1, kmalloc'tan */
static char address[WEB_URL_MAX],current[WEB_URL_MAX],previous[WEB_URL_MAX];
static unsigned body_size;
static int address_focus=1,select_all=1,scroll,changed=1,loading,last_state,redirects,status_code;
static int view_w,view_h,rows,columns=90;
static int local_mode;
static const char *message="HTML okuyucu · HTTP / HTTPS";
static const char *bookmark_labels[]={"Masaüstü","Sistem","Ağ","Hakkında","Google","Wikipedia"};

static void text(gpu_surface_t *s,int x,int y,const char *str,uint32_t color) { ui_text(s,x,y,str,color,0); }
static void rect(gpu_surface_t *s,int x,int y,int w,int h,uint32_t color) { if(w>0&&h>0) gpu_fill_rect(s,x,y,w,h,color); }
static void request_page(const char *url,int remember) {
    web_url_t parsed; const char *error; char request[1536],port[16];
    /* Copy before cancel/reset: the supplied URL may point to our own buffers. */
    char next[WEB_URL_MAX];
    if(kstrlen(url)>=WEB_URL_MAX) { message="Adres çok uzun."; changed=1; return; }
    kstrcpy(next,url); net_http_cancel(); loading=0;

    /* Local VFS File Loading (file://, ui://, /ui/...) */
    if(!kstrncmp(next, "file://", 7) || !kstrncmp(next, "ui://", 5) || next[0] == '/') {
        const char *path = next;
        if(!kstrncmp(next, "file://", 7)) path = next + 7;
        else if(!kstrncmp(next, "ui://", 5)) {
            static char ui_path[WEB_URL_MAX];
            if(kstrlen(next+5)>WEB_URL_MAX-10){message="Adres çok uzun.";changed=1;return;}
            kstrcpy(ui_path, "/ui/");
            kstrcat(ui_path, next + 5);
            int ulen = kstrlen(ui_path);
            if(ulen < 5 || kstrcmp(ui_path + ulen - 5, ".html")) kstrcat(ui_path, ".html");
            path = ui_path;
        }
        vfs_node_t *node = fs_lookup(path);
        if(!node || node->type == NODE_DIR) {
            message = "Dosya bulunamadı.";
            changed = 1;
            return;
        }
        const uint8_t *fdata = fs_node_data(node);
        uint32_t fsize = node->size;
        if(fsize > NET_HTTP_CAP) fsize = NET_HTTP_CAP;
        kmemcpy(body, fdata, fsize);
        body[fsize] = 0;
        body_size = fsize;
        if(remember && current[0] && kstrcmp(next, current)) kstrcpy(previous, current);
        kstrcpy(current, next);
        kstrcpy(address, next);
        kstrcpy(page.title, node->name);
        select_all = 0; address_focus = 0; scroll = 0;
        web_page_parse(body, body_size, &page, columns);
        message=axis_web_load(3,path,body,body_size,1)?"Yerel sayfa · AXIS Web":axis_web_status(3);
        changed = 1;
        return;
    }

    if(!web_url_parse(next,&parsed,&error)) { message=error; changed=1; return; }
    if(remember&&current[0]&&kstrcmp(next,current)) kstrcpy(previous,current);
    kstrcpy(current,next); kstrcpy(address,next); select_all=0; address_focus=0; scroll=0;
    /* Never show a previous origin's content beneath a new URL or TLS error. */
    body_size=0; body[0]=0; kmemset(&page,0,sizeof(page));
    axis_web_close(3);
    kstrcpy(page.title,"Bağlanıyor...");
    kstrcpy(request,"GET "); kstrcat(request,parsed.path); kstrcat(request," HTTP/1.1\r\nHost: "); kstrcat(request,parsed.host);
    if(parsed.port!=(parsed.secure?443:80)) { kstrcat(request,":"); kitoa(parsed.port,port); kstrcat(request,port); }
    kstrcat(request,"\r\nUser-Agent: AXIS-Web/1.0 (Lexbor; AXIS OS)\r\n"
                    "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,video/mpeg,*/*;q=0.8\r\n"
                    "Accept-Language: tr-TR,tr;q=0.9,en-US;q=0.8,en;q=0.7\r\n"
                    "Accept-Encoding: identity\r\n"
                    "Connection: close\r\n\r\n");
    loading=net_web_start(parsed.host,parsed.port,request,parsed.secure);
    message=loading?"Connecting...":net_http_error(); last_state=-1; changed=1;
}
void browser_navigate(const char *url) { redirects=0; request_page(url,1); }
void browser_init(void) {
    if (!body) body = (char *)kmalloc(NET_HTTP_CAP + 1);
    chromium_engine_init();
    html_ui_init();
    browser_navigate("file:///ui/index.html");
}
void browser_draw(gpu_surface_t *s,int x,int y,int w,int h) {
    view_w=w; view_h=h; rows=(h-148)/26;
    if(rows<1) rows=1;
    int next_columns=(w-80)/9; if(next_columns>99) next_columns=99; if(next_columns<30) next_columns=30;
    if(next_columns!=columns) { columns=next_columns; web_page_parse(body,body_size,&page,columns); }
    if(scroll<0) scroll=0;

    rect(s,x,y,w,h,UI_PANEL);
    if (local_mode) goto render_page;

    /* 1. SEKMELER ÇUBUĞU (y .. y+32) */
    rect(s,x,y,w,32,UI_BG);
    /* Aktif Sekme */
    int tab_w = 220; if(tab_w > w - 180) tab_w = w - 180;
    ui_round(s,x+12,y+4,tab_w,28,9,UI_PANEL);

    char tab_title[32];
    const char *src_title = page.title[0] ? page.title : "New Tab";
    unsigned tlen = 0;
    while(src_title[tlen] && tlen < 22) { tab_title[tlen] = src_title[tlen]; tlen++; }
    tab_title[tlen] = 0;
    text(s,x+26,y+10,tab_title,UI_TEXT);
    text(s,x+tab_w-8,y+10,"x",UI_MUTED);
    /* Yeni Sekme [+] Butonu */
    rect(s,x+tab_w+20,y+6,24,24,UI_RAISED);
    text(s,x+tab_w+28,y+9,"+",UI_TEXT);

    /* DevTools [ ⚡ DevTools / JS ] Butonu */
    int devbtn_w = 114;
    int devbtn_x = x + w - devbtn_w - 12;
    rect(s, devbtn_x, y + 4, devbtn_w, 24, g_devtools.open ? UI_ACCENT : UI_RAISED);
    gpu_draw_rect(s, devbtn_x, y + 4, devbtn_w, 24, g_devtools.open ? UI_ACCENT : UI_LINE, 1);
    text(s, devbtn_x + 8, y + 8, g_devtools.open ? "Kapat" : "Geliştirici", g_devtools.open ? 0xffffffffu : UI_TEXT);

    rect(s,x,y+32,w,1,UI_LINE);

    /* 2. GEZİNTİ VE ADRES ÇUBUĞU (y+33 .. y+74) */
    int nav_y = y + 33;
    rect(s,x,nav_y,w,42,UI_RAISED);
    /* Geri Butonu */
    text(s,x+16,nav_y+13,"<",previous[0]?UI_TEXT:UI_MUTED);
    /* Yenile / Durdur Butonu */
    text(s,x+46,nav_y+13,loading?"X":"R",UI_TEXT);
    /* SSL Kilit Rozeti */
    int ssl_w = 70;
    int is_sec = net_http_secure() || (current[0] && !kstrncmp(current,"https://",8));
    rect(s,x+76,nav_y+7,ssl_w,28,is_sec ? 0x2534d399u : UI_HOVER);
    gpu_draw_rect(s,x+76,nav_y+7,ssl_w,28,is_sec ? 0xff059669u : UI_LINE,1);
    text(s,x+84,nav_y+13,!kstrncmp(current,"file://",7)?"Yerel":is_sec?"HTTPS":"HTTP",is_sec ? 0xff34d399u : UI_MUTED);

    /* Adres & Arama Çubuğu */
    int bar_x = x + 154, bar_w = w - 240;
    if(bar_w < 100) bar_w = 100;
    ui_round(s,bar_x,nav_y+6,bar_w,30,9,address_focus?UI_ACCENT:UI_LINE);
    ui_round(s,bar_x+1,nav_y+7,bar_w-2,28,8,UI_PANEL);

    gpu_surface_t field=*s; field.pixels=s->pixels+(nav_y+12)*s->stride+bar_x+10;
    field.w=bar_w-20; field.h=22;
    const char *visible=address;
    while(*visible&&ui_text_measure(visible,0)>(int)field.w-8) visible++;
    if(address_focus&&select_all) rect(&field,0,0,ui_text_measure(visible,0),20,UI_HOVER);
    text(&field,0,0,visible,UI_TEXT);
    if(address_focus&&!select_all) rect(&field,ui_text_measure(visible,0)+2,1,1,18,UI_ACCENT);

    /* Git / Ara Butonu */
    int go_x = bar_x + bar_w + 10;
    ui_round(s,go_x,nav_y+6,62,30,9,UI_ACCENT);
    text(s,go_x+18,nav_y+12,"Git",0xffffffffu);
    rect(s,x,nav_y+42,w,1,UI_LINE);

    /* 3. YER İMLERİ ÇUBUĞU (y+76 .. y+106) */
    int bmk_y = nav_y + 43;
    rect(s,x,bmk_y,w,30,UI_BG);
    int bx = x + 16;
    for(int b=0; b<6; b++) {
        int bw = ui_text_measure(bookmark_labels[b],0) + 16;
        ui_round(s,bx,bmk_y+4,bw,22,7,UI_RAISED);
        text(s,bx+8,bmk_y+7,bookmark_labels[b],UI_TEXT);
        bx += bw + 8;
    }
    rect(s,x,bmk_y+30,w,1,UI_LINE);

    /* 4. SAYFA İÇERİĞİ (y+107 .. y+h-30) */
    int dev_h = g_devtools.open ? g_devtools.height : 0;
    int content_y = bmk_y + 32;
    int content_h = h - (content_y - y) - 30 - dev_h;
render_page:
    if (local_mode) { content_y = y; content_h = h; }
    if(content_h < 40) content_h = 40;
    if (!local_mode) ui_text_fit(s,x+24,content_y+6,w-48,page.title,UI_TEXT,0);

    gpu_surface_t content=*s; content.pixels=s->pixels+(local_mode?content_y:content_y+32)*s->stride+x+(local_mode?0:24);
    content.w=local_mode?w:w-56; content.h=local_mode?content_h:content_h-36;
    rows=content.h/26; if(rows<1)rows=1;
    axis_web_draw(3,&content,0,0,content.w,content.h);

    /* 4.5. CHROMIUM DEVTOOLS ÇEKMECESİ */
    if(!local_mode && g_devtools.open) {
        chromium_devtools_draw(s, x, y + h - 30 - dev_h, w, dev_h);
    }

    /* 5. DURUM ÇUBUĞU (y+h-28 .. y+h) */
    if (local_mode) return;
    rect(s,x,y+h-28,w,1,UI_LINE);
    gpu_surface_t status=*s; status.pixels=s->pixels+(y+h-22)*s->stride+x+16; status.w=w-32; status.h=22;
    text(&status,0,0,message,loading?UI_ACCENT:UI_MUTED);
    const char *brand = "Jade";
    int brand_w = ui_text_measure(brand,0);
    if(status.w > (unsigned)brand_w + 20) {
        text(&status,status.w - brand_w - 10,0,brand,UI_MUTED);
    }
}
void browser_draw_local(gpu_surface_t *s,int x,int y,int w,int h) { local_mode=1; browser_draw(s,x,y,w,h); local_mode=0; }
int browser_key(char key) {
    if((uint8_t)key == KBD_F12) {
        g_devtools.open = !g_devtools.open;
        changed = 1;
        return 1;
    }
    if(g_devtools.open && chromium_devtools_key(key)) {
        changed = 1;
        return 1;
    }
    if(key==12) { address_focus=select_all=1; changed=1; return 1; } /* Ctrl+L */
    if(key==1&&address_focus) { select_all=1; changed=1; return 1; }
    if(key==KBD_ESCAPE) { if(loading) { net_http_cancel(); loading=0; message="İndirme durduruldu."; } address_focus=0; changed=1; return 1; }
    if(address_focus) {
        unsigned len=kstrlen(address);
        if(key=='\n') { browser_navigate(address); return 1; }
        if(key=='\b'||key==KBD_DEL) { if(select_all) address[0]=0; else if(len) address[len-1]=0; select_all=0; changed=1; }
        else if(key>=32&&key<127) {
            if(select_all) { address[0]=0; len=0; select_all=0; }
            if(len+1<sizeof(address)) { address[len]=key; address[len+1]=0; changed=1; }
        }
        return 1;
    }
    if(axis_web_key(3,key)){changed=1;return 1;}
    return 1;
}
void browser_click(int x,int y) {
    if (local_mode) goto local_page;
    /* DevTools Aç / Kapat Butonu */
    if(y < 32 && x >= view_w - 130) {
        g_devtools.open = !g_devtools.open;
        changed = 1;
        return;
    }
    int dev_h = g_devtools.open ? g_devtools.height : 0;
    if(g_devtools.open && y >= view_h - 30 - dev_h && y < view_h - 30) {
        chromium_devtools_click(x, y - (view_h - 30 - dev_h), view_w, dev_h);
        changed = 1;
        return;
    }
    /* 1. Sekmeler Çubuğu (y < 32) */
    if(y < 32) {
        if(x >= 12 && x < 240) {
            /* Sekme tıklandı: yenile */
            if(current[0]) browser_navigate(current);
        } else if(x >= 240 && x < 275) {
            /* Yeni Sekme [+] */
            browser_init();
            changed=1;
        }
        return;
    }
    /* 2. Gezinti ve Adres Çubuğu (y >= 33 && y < 75) */
    if(y >= 33 && y < 75) {
        if(x < 38 && previous[0]) { char back[WEB_URL_MAX]; kstrcpy(back,previous); browser_navigate(back); }
        else if(x >= 38 && x < 74) {
            if(loading) { net_http_cancel(); loading=0; message="İndirme durduruldu."; }
            else if(current[0]) browser_navigate(current);
        }
        else if(x >= 74 && x < 150) {
            /* SSL rozeti: bilgi göster */
            message = !kstrncmp(current,"file://",7) ? "Bu sayfa yerel dosyalardan yüklendi." :
                net_http_secure() ? "HTTPS bağlantısı şifreli." : "Bağlantı şifrelenmemiş (HTTP).";
            changed = 1;
        }
        else if(x >= 154 && x < view_w - 75) address_focus=select_all=1;
        else browser_navigate(address);
        changed=1; return;
    }
    /* 3. Yer İmleri Çubuğu (y >= 76 && y < 106) */
    if(y >= 76 && y < 106) {
        const char *urls[]={"file:///ui/index.html", "file:///ui/system.html", "file:///ui/network.html", "file:///ui/about.html", "https://www.google.com/", "https://en.wikipedia.org/"};
        int bx=16;
        for(int b=0;b<6;b++) {
            int bw=ui_text_measure(bookmark_labels[b],0)+16;
            if(x>=bx&&x<bx+bw) { browser_navigate(urls[b]); break; }
            bx+=bw+8;
        }
        changed = 1; return;
    }
local_page:
    address_focus=0; changed=1;
    {
        char action[WEB_URL_MAX];int top=local_mode?0:141;
        if(axis_web_click(3,x-(local_mode?0:24),y-top,action,sizeof(action))) {
            if(action[0]&&kstrncmp(action,"ipc:",4))browser_navigate(action);
            return;
        }
    }
}
void browser_click_local(int x,int y) { local_mode=1; browser_click(x,y); local_mode=0; }
int browser_poll(void) {
    int now=net_http_state();
    if(loading&&now!=last_state) {
        last_state=now; changed=1;
        if(now==NET_DNS) message="Adres çözümleniyor...";
        if(now==NET_CONNECT) message="Sunucuya bağlanıyor...";
        if(now==NET_RECEIVE) message="Sayfa indiriliyor...";
        if(now==NET_ERROR) { loading=0; message=net_http_error(); }
        if(now==NET_IDLE) { loading=0; message="Bağlantı kapatıldı."; }
        if(now==NET_DONE) {
            char location[WEB_URL_MAX]; const char *error; int media;
            loading=0;
            if(web_response_decode(net_http_data(),net_http_size(),body,NET_HTTP_CAP,&body_size,&status_code,location,&error,&media)) {
                if(status_code>=300&&status_code<400&&location[0]) {
                    char next[WEB_URL_MAX];
                    if(++redirects>5) message="Çok fazla yönlendirme.";
                    else if(web_url_resolve(current,location,next)) {
                        if(!kstrncmp(current,"https://",8)&&!kstrncmp(next,"http://",7)) message="Güvensiz HTTPS → HTTP yönlendirmesi engellendi.";
                        else request_page(next,0);
                    }
                    else message="Geçersiz yönlendirme adresi.";
                } else if(media) {
                    if(status_code>=400) message="Video sunucusu hata döndürdü.";
                    else if(media_load(body,body_size,current)) { ui_open_media(); message="Video Medya uygulamasında oynatılıyor."; }
                    else message=media_status();
                    /* Binary media never enters the text layout path. */
                    body_size=0; body[0]=0; kmemset(&page,0,sizeof(page));
                    kstrcpy(page.title,"Medya bağlantısı");
                } else {
                    if(!web_text_utf8(net_http_data(),net_http_size(),body,&body_size,NET_HTTP_CAP)) {
                        body_size=0; body[0]=0; message="Metin kodlaması boyut sınırını aşıyor."; return 1;
                    }
                    web_page_parse(body,body_size,&page,columns); scroll=0;
                    axis_web_load(3,current,body,body_size,0);
                    message=status_code>=400?"Sunucu hata sayfası döndürdü.":"Yüklendi · AXIS Web";
                }
            } else message=error;
        }
    }
    int result=changed; changed=0; return result;
}
const char *browser_status(void) { return message; }
const char *browser_title(void) { return page.title; }
const char *browser_current_url(void) { return current; }
const char *browser_text_line(unsigned line) { return line<page.count?page.lines[line].text:""; }
