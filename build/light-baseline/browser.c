#include "browser.h"
#include "web.h"
#include "net.h"
#include "ui_font.h"
#include "theme.h"
#include "keyboard.h"

static web_page_t page;
static char body[NET_HTTP_CAP+1],address[WEB_URL_MAX],current[WEB_URL_MAX],previous[WEB_URL_MAX];
static unsigned body_size;
static int address_focus=1,select_all=1,scroll,changed=1,loading,last_state,redirects,status_code;
static int view_w,view_h,rows,columns=90;
static const char *message="HTTP sayfaları için hafif tarayıcı";

static void text(gpu_surface_t *s,int x,int y,const char *str,uint32_t color) { ui_text(s,x,y,str,color,0); }
static void rect(gpu_surface_t *s,int x,int y,int w,int h,uint32_t color) { if(w>0&&h>0) gpu_fill_rect(s,x,y,w,h,color); }
static void request_page(const char *url,int remember) {
    web_url_t parsed; const char *error; char request[1024],port[16];
    /* Copy before cancel/reset: the supplied URL may point to our own buffers. */
    char next[WEB_URL_MAX];
    if(kstrlen(url)>=WEB_URL_MAX) { message="Adres çok uzun."; changed=1; return; }
    kstrcpy(next,url); net_http_cancel(); loading=0;
    if(!web_url_parse(next,&parsed,&error)) { message=error; changed=1; return; }
    if(remember&&current[0]&&kstrcmp(next,current)) kstrcpy(previous,current);
    kstrcpy(current,next); kstrcpy(address,next); select_all=0; address_focus=0; scroll=0;
    kstrcpy(request,"GET "); kstrcat(request,parsed.path); kstrcat(request," HTTP/1.1\r\nHost: "); kstrcat(request,parsed.host);
    if(parsed.port!=80) { kstrcat(request,":"); kitoa(parsed.port,port); kstrcat(request,port); }
    kstrcat(request,"\r\nUser-Agent: Shenlong/0.5\r\nAccept: text/html, text/plain\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
    loading=net_http_start(parsed.host,parsed.port,request);
    message=loading?"Bağlanıyor...":net_http_error(); last_state=-1; changed=1;
}
void browser_navigate(const char *url) { redirects=0; request_page(url,1); }
void browser_init(void) {
    static const char welcome[]=
        "<title>Shenlong Web</title><h1>Web, sade haliyle.</h1>"
        "<p>Adres çubuğuna bir HTTP adresi yaz ve Enter'a bas.</p>"
        "<p>Hostun internet bağlantısı kullanılır. HTTP şifrelenmez.</p>"
        "<p>Bu sürüm HTML metinlerini ve bağlantıları gösterir; HTTPS, JavaScript ve görseller desteklenmez.</p>"
        "<h2>Başlangıç</h2><a href=\"http://example.com/\">Example Domain</a>"
        "<h2>Kısayollar</h2><p>Ctrl+L: adres   /   Enter: git   /   Esc: durdur</p>"
        "<p>Yukarı-Aşağı veya Page Up-Page Down: sayfayı kaydır.</p>";
    kstrcpy(body,welcome); body_size=sizeof(welcome)-1;
    web_page_parse(body,body_size,&page,columns); kstrcpy(address,"http://example.com/");
}
void browser_draw(gpu_surface_t *s,int x,int y,int w,int h) {
    view_w=w; view_h=h; rows=(h-128)/26;
    if(rows<1) rows=1;
    int next_columns=(w-80)/9; if(next_columns>99) next_columns=99; if(next_columns<30) next_columns=30;
    if(next_columns!=columns) { columns=next_columns; web_page_parse(body,body_size,&page,columns); }
    if(scroll>(int)page.count-rows) scroll=(int)page.count-rows; if(scroll<0) scroll=0;
    rect(s,x,y,w,h,UI_PANEL);
    rect(s,x,y,w,60,UI_RAISED);
    text(s,x+16,y+18,"<",previous[0]?UI_TEXT:UI_MUTED);
    text(s,x+45,y+18,loading?"X":"R",UI_TEXT);
    rect(s,x+80,y+10,w-164,38,UI_BG);
    gpu_draw_rect(s,x+80,y+10,w-164,38,address_focus?UI_ACCENT:UI_LINE,1);
    /* Clip a long URL to the address field, independently of the window. */
    gpu_surface_t field=*s; field.pixels=s->pixels+(y+17)*s->stride+x+90;
    field.w=w-184; field.h=26;
    const char *visible=address;
    while(*visible&&ui_text_measure(visible,0)>(int)field.w-8) visible++;
    if(address_focus&&select_all) rect(&field,0,0,ui_text_measure(visible,0),24,0xff27443fu);
    text(&field,0,0,visible,UI_TEXT);
    if(address_focus&&!select_all) rect(&field,ui_text_measure(visible,0)+2,2,1,20,UI_ACCENT);
    text(s,x+w-66,y+18,"Git",UI_ACCENT);
    rect(s,x,y+60,w,1,UI_LINE);
    text(s,x+24,y+76,page.title,UI_TEXT);
    gpu_surface_t content=*s; content.pixels=s->pixels+(y+108)*s->stride+x+28;
    content.w=w-64; content.h=h-148;
    for(int row=0;row<rows&&(unsigned)(scroll+row)<page.count;row++) {
        web_line_t *line=&page.lines[scroll+row];
        uint32_t color=line->link>=0?UI_ACCENT:(line->heading?UI_TEXT:UI_MUTED);
        text(&content,0,row*26,line->text,color);
        if(line->link>=0) rect(&content,0,row*26+22,ui_text_measure(line->text,0),1,0xff365e55u);
    }
    if((int)page.count>rows) {
        int track=h-152,thumb=track*rows/page.count; if(thumb<16) thumb=16;
        rect(s,x+w-15,y+108,3,track,UI_RAISED);
        rect(s,x+w-15,y+108+scroll*(track-thumb)/(page.count-rows),3,thumb,UI_ACCENT);
    }
    rect(s,x,y+h-34,w,1,UI_LINE);
    gpu_surface_t status=*s; status.pixels=s->pixels+(y+h-27)*s->stride+x+20; status.w=w-40; status.h=26;
    text(&status,0,0,message,loading?UI_ACCENT:UI_MUTED);
}
int browser_key(char key) {
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
    if(key==KBD_DOWN||key==KBD_PGDN||key==KBD_UP||key==KBD_PGUP) {
        int delta=(key==KBD_PGDN||key==KBD_PGUP)?rows:1;
        if(key==KBD_UP||key==KBD_PGUP) delta=-delta;
        scroll+=delta; if(scroll<0) scroll=0;
        if(scroll>(int)page.count-rows) scroll=(int)page.count-rows; if(scroll<0) scroll=0;
        changed=1;
    }
    return 1;
}
void browser_click(int x,int y) {
    if(y<60) {
        if(x<38&&previous[0]) { char back[WEB_URL_MAX]; kstrcpy(back,previous); browser_navigate(back); }
        else if(x<78) { if(loading) { net_http_cancel(); loading=0; message="İndirme durduruldu."; } else if(current[0]) browser_navigate(current); }
        else if(x<view_w-84) address_focus=select_all=1;
        else browser_navigate(address);
        changed=1; return;
    }
    address_focus=0; changed=1;
    if(y>=108&&y<view_h-40) {
        unsigned row=scroll+(y-108)/26;
        if(row<page.count&&page.lines[row].link>=0) {
            int link=page.lines[row].link; char url[WEB_URL_MAX];
            const char *base=current[0]?current:"http://example.com/";
            if(web_url_resolve(base,page.links[link].url,url)) browser_navigate(url);
            else message="Bu bağlantı türü desteklenmiyor.";
        }
    }
}
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
            char location[WEB_URL_MAX]; const char *error;
            loading=0;
            if(web_http_decode(net_http_data(),net_http_size(),body,sizeof(body),&body_size,&status_code,location,&error)) {
                if(status_code>=300&&status_code<400&&location[0]) {
                    char next[WEB_URL_MAX];
                    if(++redirects>5) message="Çok fazla yönlendirme.";
                    else if(web_url_resolve(current,location,next)) request_page(next,0);
                    else message="Geçersiz yönlendirme adresi.";
                } else {
                    web_page_parse(body,body_size,&page,columns); scroll=0;
                    message=status_code>=400?"Sunucu hata sayfası döndürdü.":(page.truncated?"Sayfa metin sınırında kesildi.":"Yüklendi  ·  HTTP / Şifrelenmemiş bağlantı");
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
