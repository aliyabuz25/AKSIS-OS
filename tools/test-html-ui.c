/* Exercise actual Jade parsing/layout, raster clipping and native icon scaling. */
#include "../kernel/html_ui.h"
#include "../kernel/icons.h"
#include "../kernel/kernel_info.h"
extern void *malloc(size_t);
extern void free(void *);
extern int printf(const char *,...);
sysinfo_t g_sysinfo;
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void *kmemset(void *p,int c,size_t n) { uint8_t *d=p; while(n--) *d++=c; return p; }
int kstrlen(const char *s) { int n=0; while(s[n]) n++; return n; }
int kstrncmp(const char *a,const char *b,int n) {
    while(n-- > 0) { if(*a!=*b) return (uint8_t)*a-(uint8_t)*b; if(!*a) break; a++; b++; }
    return 0;
}
char *kstrcpy(char *d,const char *s) { char *out=d; while((*d++=*s++)) {} return out; }
char *kstrcat(char *d,const char *s) { kstrcpy(d+kstrlen(d),s); return d; }
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
static html_ui_doc_t doc;
static uint32_t pixels[1032*640];
int main(void) {
    html_ui_parse("<!DOCTYPE html><html><head><title>Hidden</title></head><body>"
                  "<div class='card'><h1 class='card-title'>Heading</h1>"
                  "<p class='card-text'>Body</p></div><p class='discard'>Plain</p></body></html>",&doc);
    CHECK(doc.count==6);
    CHECK(doc.nodes[3].type==HTML_NODE_HEADING && !doc.nodes[3].style.bg_color);
    CHECK(doc.nodes[4].type==HTML_NODE_TEXT && !doc.nodes[4].style.bg_color);
    CHECK(doc.nodes[5].type==HTML_NODE_TEXT && !doc.nodes[5].style.bg_color);
    CHECK(doc.nodes[5].parent==1);
    const char *pages[]={html_ui_get_welcome_page(),html_ui_get_about_page(),
                         html_ui_get_system_page(),html_ui_get_network_page()};
    int widths[]={320,480,800,976};
    for(int page=0;page<4;page++) for(int wi=0;wi<4;wi++) {
        int width=widths[wi];
        CHECK(html_ui_parse(pages[page],&doc)>10);
        html_ui_layout(&doc,width,600);
        int links=0;
        for(int i=0;i<doc.count;i++) {
            html_ui_node_t *n=&doc.nodes[i];
            CHECK(n->w>0 && n->h>=0 && n->x>=0 && n->x+n->w<=width);
            if(n->parent>=0) {
                html_ui_node_t *p=&doc.nodes[n->parent];
                CHECK(n->x>=p->x && n->x+n->w<=p->x+p->w);
                CHECK(n->y>=p->y && n->y+n->h<=p->y+p->h);
            }
            if(n->href[0]) {
                char action[144];
                CHECK(html_ui_click(&doc,n->x+1,n->y+1,0,action,sizeof(action)));
                CHECK(!kstrncmp(action,n->href,sizeof(action)));
                links++;
            }
        }
        CHECK(links>=3);
        for(int i=0;i<1032*640;i++) pixels[i]=0x12345678;
        gpu_surface_t view={.pixels=pixels+1032*10+12,.w=width,.h=600,.stride=1032};
        gpu_fill(&view,0xff181818);
        html_ui_render(&view,0,0,60,&doc);
        for(int y=0;y<640;y++) for(int x=0;x<1032;x++)
            if(y<10||y>=610||x<12||x>=12+width) CHECK(pixels[y*1032+x]==0x12345678);
    }
    /* At taskbar size, the play triangle must not vanish during vector scaling. */
    gpu_surface_t icon={.pixels=pixels,.w=24,.h=24,.stride=24};
    gpu_fill(&icon,0xff000000); ui_icon(&icon,0,0,5,24);
    CHECK(GPU_R(pixels[12*24+11])>200);
    printf("PASS: class tokens, declarations, page bounds at 320/480/800/976px, links, clipped raster, 24px vector icon.\n");
    return 0;
}
