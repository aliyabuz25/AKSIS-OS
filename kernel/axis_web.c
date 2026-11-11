#include "axis_web.h"
#include "axis_web/runtime.h"
#include "html_ui.h"
#include "fs.h"
#include "keyboard.h"
#include "kernel_info.h"
#include "clock.h"
#include "ui_font.h"
#include "ui_paint.h"
#include "theme.h"
#include "chromium_engine.h"

typedef struct { aw_view *view;html_ui_doc_t *layout;char path[256];int scroll,w,h,dirty; } app_view;
static app_view apps[6];
static aw_node *exported;
static char *interpolated;
static int initialized;
static unsigned changed_apps;
static void copy_string(char *out,unsigned cap,const char *s){unsigned i=0;if(!cap)return;while(s[i]&&i+1<cap){out[i]=s[i];i++;}out[i]=0;}
static unsigned long long host_now(void *p){(void)p;return get_uptime_ms();}
static void host_log(void *p,const char *s){(void)p;chromium_console_log(s);while(*s)outb(0x3f8,*s++);outb(0x3f8,'\n');}
static const char *host_read(void *p,const char *path,unsigned long *size){
    (void)p;
    /* Local application resources are confined to /ui. Remote documents have
       no VFS loader and cannot execute page scripts in the kernel. */
    if(kstrncmp(path,"/ui/",4))return NULL;
    vfs_node_t *n=fs_lookup(path);if(!n||n->type!=NODE_FILE)return NULL;
    *size=n->size;return (const char *)fs_node_data(n);
}
unsigned long long axis_wall_time_ms(void){
    wall_time_t t;if(!clock_read(&t,0))return get_uptime_ms();
    int y=t.year,m=t.month;y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yo=y-era*400;
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+t.day-1;
    long long days=(long long)era*146097+yo*365+yo/4-yo/100+doy-719468;
    return ((days*24+t.hour)*60*60+t.minute*60+t.second)*1000ULL;
}
void axis_web_init(void){
    if(initialized)return;initialized=1;
    axis_memory_init(kmalloc(48*1024*1024),48*1024*1024);
    exported=kmalloc(sizeof(aw_node)*HTML_UI_MAX_NODES);interpolated=kmalloc(1024*1024+1);
}
static void interpolate(const char *src,unsigned size,char *out,unsigned cap){
    unsigned n=0;
    for(unsigned i=0;i<size&&n+1<cap;){
        const char *value=NULL;unsigned skip=0;char buf[64];
        if(i+7<=size&&!kstrncmp(src+i,"{{RAM}}",7)){fmt_size(g_sysinfo.total_memory,buf);value=buf;skip=7;}
        else if(i+7<=size&&!kstrncmp(src+i,"{{CPU}}",7)){value=get_cpu_brand_str();skip=7;}
        if(value){while(*value&&n+1<cap)out[n++]=*value++;i+=skip;}
        else out[n++]=src[i++];
    }out[n]=0;
}
int axis_web_load(int app,const char *url,const char *html,unsigned size,int scripts){
    if(app<1||app>4||size>1024*1024)return 0;axis_web_init();app_view *a=&apps[app];
    if(!a->layout)a->layout=kmalloc(sizeof(*a->layout));
    if(a->view)aw_destroy(a->view);
    aw_host host={.now=host_now,.log=host_log,.read=scripts?host_read:NULL};a->view=aw_create(&host);
    a->scroll=0;a->dirty=1;changed_apps|=1u<<app;copy_string(a->path,sizeof(a->path),url);
    if(!a->view)return 0;
    interpolate(html,size,interpolated,1024*1024+1);
    int ok=aw_load(a->view,url,interpolated,kstrlen(interpolated),scripts);
    if(!ok)a->layout->count=0;return ok;
}
int axis_web_open(int app,const char *path){
    char resolved[256];if(!aw_resolve("/ui/index.html",path,resolved,sizeof(resolved)))return 0;
    unsigned long size=0;const char *html=host_read(NULL,resolved,&size);if(!html)return 0;
    return axis_web_load(app,resolved,html,size,1);
}
void axis_web_close(int app){if(app<1||app>4)return;if(apps[app].view)aw_destroy(apps[app].view);apps[app].view=NULL;if(apps[app].layout)apps[app].layout->count=0;}
const char *axis_web_status(int app){return app>=1&&app<=4?aw_error(apps[app].view):"Invalid application";}
void axis_web_draw(int app,gpu_surface_t *s,int x,int y,int w,int h){
    if(app<1||app>4||!s||w<=0||h<=0||x<0||y<0||x+w>(int)s->w||y+h>(int)s->h)return;
    app_view *a=&apps[app];
    gpu_surface_t content=*s;content.pixels=s->pixels+y*s->stride+x;content.w=w;content.h=h;
    gpu_fill(&content,0xfff7f7f7);
    if(!a->view){ui_text(&content,20,20,"Sayfa yüklenemedi.",UI_TEXT,0);return;}
    if(a->dirty||a->w!=w||a->h!=h){
        int count=aw_export(a->view,exported,HTML_UI_MAX_NODES);html_ui_import(exported,count,a->layout);html_ui_layout(a->layout,w,h);
        a->w=w;a->h=h;a->dirty=0;
    }
    int max=a->layout->content_height-h;if(max<0)max=0;if(a->scroll>max)a->scroll=max;
    html_ui_render(&content,0,0,a->scroll,a->layout);
    if(max){int thumb=h*h/a->layout->content_height;if(thumb<24)thumb=24;gpu_fill_rect(&content,w-5,a->scroll*(h-thumb)/max,3,thumb,0xffa0a0a0);}
    if(!a->layout->count)ui_text_fit(&content,20,20,w-40,aw_error(a->view),0xffa02020,0);
}
int axis_web_click(int app,int x,int y,char *action,unsigned cap){
    if(cap)action[0]=0;if(app<1||app>4)return 0;app_view *a=&apps[app];if(!a->view||!a->layout)return 0;
    if(x<0||y<0||x>=a->w||y>=a->h)return 0;y+=a->scroll;
    for(int i=a->layout->count-1;i>=0;i--){html_ui_node_t *n=&a->layout->nodes[i];
        if(x<n->x||x>=n->x+n->w||y<n->y||y>=n->y+n->h)continue;
        int prevented=aw_click(a->view,n->handle);a->dirty=1;changed_apps|=1u<<app;
        if(prevented)return 1;
        int index=i;while(index>=0&&!a->layout->nodes[index].href[0])index=a->layout->nodes[index].parent;
        if(index>=0){const char *href=a->layout->nodes[index].href;
            if(!kstrncmp(href,"ipc:",4)){copy_string(action,cap,href);return 1;}
            if(href[0]=='#')return 1;
            char path[256];if(aw_resolve(a->path,href,path,sizeof(path))){axis_web_open(app,path);return 1;}
            copy_string(action,cap,href);
        }return 1;
    }return 0;
}
int axis_web_key(int app,char key){
    if(app<1||app>4)return 0;app_view *a=&apps[app];int delta=0;
    if(key==KBD_DOWN)delta=32;else if(key==KBD_UP)delta=-32;else if(key==KBD_PGDN)delta=a->h-32;else if(key==KBD_PGUP)delta=32-a->h;else return 0;
    a->scroll+=delta;if(a->scroll<0)a->scroll=0;changed_apps|=1u<<app;return 1;
}
unsigned axis_web_poll(void){for(int i=1;i<=4;i++)if(apps[i].view&&aw_poll(apps[i].view)){apps[i].dirty=1;changed_apps|=1u<<i;}unsigned result=changed_apps;changed_apps=0;return result;}
int axis_web_eval(int app,const char *script,char *out,unsigned cap){if(app<1||app>4)return 0;int ok=aw_eval(apps[app].view,script,out,cap);apps[app].dirty=1;changed_apps|=1u<<app;if(!ok&&cap)copy_string(out,cap,axis_web_status(app));return ok;}
