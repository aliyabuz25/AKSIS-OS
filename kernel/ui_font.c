#include "ui_font.h"
typedef struct { unsigned code, width, advance, offset; } ui_glyph_t;
typedef struct { const ui_glyph_t *glyphs; const uint8_t *alpha; unsigned count, height; } ui_font_t;
#include "ui_font_data.h"

static unsigned decode(const char **str) {
    const unsigned char *p=(const unsigned char *)*str;
    unsigned c=*p++;
    if ((c&0xe0)==0xc0 && (*p&0xc0)==0x80) { c=((c&31)<<6)|(*p++&63); }
    *str=(const char *)p; return c;
}
static const ui_glyph_t *glyph(const ui_font_t *f, unsigned c) {
    for (unsigned i=0;i<f->count;i++) if(f->glyphs[i].code==c) return &f->glyphs[i];
    return &f->glyphs['?'-32];
}
int ui_text_measure(const char *str,int size) {
    if(size<0||size>2) size=0;
    int w=0; while(*str) w+=glyph(&fonts[size],decode(&str))->advance;
    return w;
}
void ui_text(gpu_surface_t *s,int x,int y,const char *str,uint32_t color,int size) {
    if(!s||!s->pixels) return;
    if(size<0||size>2) size=0;
    const ui_font_t *f=&fonts[size];
    uint32_t ca=GPU_A(color);
    /* Fast path: fully opaque text. Direct 32-bit writes with bounds
       clipping instead of per-pixel alpha math (4x more pixels at 4K). */
    if(ca==255) {
        uint32_t rgb=color&0xffffff;
        while(*str) {
            const ui_glyph_t *g=glyph(f,decode(&str));
            for(unsigned py=0;py<f->height;py++) {
                unsigned yy=y+py;
                if((int)yy<0||(int)yy>=(int)s->h) continue;
                uint32_t *row=s->pixels+(uint32_t)yy*s->stride;
                const uint8_t *ga=f->alpha+g->offset+py*g->width;
                for(unsigned px=0;px<g->width;px++) {
                    unsigned xx=x+px;
                    if(xx>=s->w) break;
                    if((int)xx<0) continue;
                    uint32_t a=ga[px];
                    if(!a) continue;
                    row[xx]=(a<<24)|rgb;
                }
            }
            x+=g->advance;
        }
        return;
    }
    /* General path with global alpha (window fade-in/out) */
    while(*str) {
        const ui_glyph_t *g=glyph(f,decode(&str));
        for(unsigned py=0;py<f->height;py++) for(unsigned px=0;px<g->width;px++) {
            unsigned a=f->alpha[g->offset+py*g->width+px]*ca/255;
            if(a) gpu_put_pixel(s,x+px,y+py,(color&0xffffff)|(a<<24));
        }
        x+=g->advance;
    }
}
