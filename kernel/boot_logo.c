#include "boot_logo.h"

extern const uint8_t boot_logo_data[];

/* Fixed-point bilinear filtering is done only once during boot. */
void boot_logo_draw(gpu_surface_t *s,int x,int y,int w,int h) {
    if(!s || w<=0 || h<=0) return;
    uint32_t sw,sh;
    kmemcpy(&sw,boot_logo_data,4); kmemcpy(&sh,boot_logo_data+4,4);
    if(!sw || !sh || sw>4096 || sh>4096) return;
    int dw=w,dh=(int)((uint64_t)w*sh/sw);
    if(dh>h) { dh=h; dw=(int)((uint64_t)h*sw/sh); }
    if(!dw || !dh) return;
    x+=(w-dw)/2; y+=(h-dh)/2;
    const uint8_t *rgb=boot_logo_data+8;
    for(int py=0;py<dh;py++) {
        uint32_t fy=dh>1?(uint64_t)py*(sh-1)*256/(dh-1):0;
        uint32_t sy=fy>>8,sy1=sy+1<sh?sy+1:sy;
        unsigned ay=fy&255;
        for(int px=0;px<dw;px++) {
            uint32_t fx=dw>1?(uint64_t)px*(sw-1)*256/(dw-1):0;
            uint32_t sx=fx>>8,sx1=sx+1<sw?sx+1:sx;
            unsigned ax=fx&255;
            const uint8_t *a=rgb+(sy*sw+sx)*3,*b=rgb+(sy*sw+sx1)*3;
            const uint8_t *c=rgb+(sy1*sw+sx)*3,*d=rgb+(sy1*sw+sx1)*3;
            uint32_t color=0xff000000u;
            for(unsigned channel=0;channel<3;channel++) {
                unsigned top=a[channel]*(256-ax)+b[channel]*ax;
                unsigned bottom=c[channel]*(256-ax)+d[channel]*ax;
                unsigned v=(top*(256-ay)+bottom*ay+32768)>>16;
                color|=v<<(16-channel*8);
            }
            gpu_put_pixel(s,x+px,y+py,color);
        }
    }
}
