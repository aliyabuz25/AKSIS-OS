/* Cached native vector icons. Supersampling preserves edges at every UI size. */
#include "icons.h"
#include "ui_paint.h"

static const int sizes[]={20,22,24,32,40,48,56,64};
static uint32_t cache[11][8][64*64];
static uint8_t ready[11][8];
static uint32_t scratch[128*128];

static int scale(gpu_surface_t *s,int n) { return n*(int)s->w/64; }
static void box(gpu_surface_t *s,int x,int y,int w,int h,uint32_t c) {
    int x0=scale(s,x), y0=scale(s,y);
    gpu_fill_rect(s,x0,y0,scale(s,x+w)-x0,scale(s,y+h)-y0,c);
}
static void disc(gpu_surface_t *s,int cx,int cy,int r,uint32_t c) {
    cx=scale(s,cx); cy=scale(s,cy); r=scale(s,r);
    for(int y=-r;y<=r;y++) for(int x=-r;x<=r;x++)
        if(x*x+y*y<=r*r) gpu_put_pixel(s,cx+x,cy+y,c);
}
static void stroke(gpu_surface_t *s,int x0,int y0,int x1,int y1,int width,uint32_t c) {
    x0=scale(s,x0); x1=scale(s,x1); y0=scale(s,y0); y1=scale(s,y1);
    int dx=x1>x0?x1-x0:x0-x1,dy=y1>y0?y1-y0:y0-y1;
    int sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=dx-dy,r=scale(s,width)/2;
    for(;;) {
        gpu_fill_rect(s,x0-r,y0-r,r*2+1,r*2+1,c);
        if(x0==x1&&y0==y1) break;
        int e=2*err;
        if(e>-dy) { err-=dy; x0+=sx; }
        if(e<dx) { err+=dx; y0+=sy; }
    }
}
static void draw_vector(gpu_surface_t *s,int app) {
    const uint32_t white=0xfff5f8ffu,blue=0xff45aeefu,dark=0xff15314du;
    if(app==1||app==6) {
        box(s,4,15,23,10,0xffffce59u);
        box(s,4,23,56,32,0xffd69322u);
        box(s,7,22,50,8,0xffffe3a1u);
        box(s,4,29,56,26,0xfff3ba46u);
        box(s,4,29,56,2,0xffffd77bu);
    } else if(app==0) {
        box(s,3,9,58,46,0xff536776u); box(s,5,11,54,42,0xff192b3bu);
        box(s,5,11,54,8,0xff354e63u);
        stroke(s,15,27,24,35,2,white); stroke(s,24,35,15,43,2,white);
        box(s,31,42,15,3,0xff70c9ffu);
    } else if(app==2) {
        box(s,26,4,12,56,0xffc3d3deu); box(s,4,26,56,12,0xffc3d3deu);
        stroke(s,12,12,52,52,10,0xffc3d3deu); stroke(s,52,12,12,52,10,0xffc3d3deu);
        disc(s,32,32,23,0xffc3d3deu); disc(s,32,32,16,0xff59798fu);
        disc(s,32,32,10,white); disc(s,32,32,5,dark);
    } else if(app==3) {
        disc(s,32,32,27,0xff148cd0u); disc(s,32,32,23,0xff32b6ecu);
        box(s,8,30,48,3,white);
        for(int i=0;i<2;i++) {
            int yy=i?45:18;
            stroke(s,14,yy,50,yy,1,white);
        }
        /* Meridian ellipse in integer geometry. */
        for(int y=8;y<=56;y++) for(int x=18;x<=46;x++) {
            int dx=x-32,dy=y-32,v=dx*dx*576+dy*dy*196;
            if(v<=196*576&&v>160*576) box(s,x,y,1,1,white);
        }
        stroke(s,32,8,32,56,1,white);
    } else if(app==4) {
        box(s,4,8,56,36,0xffb3cbd9u); box(s,7,11,50,29,0xff086aa6u);
        box(s,9,13,46,25,0xff30a9dfu);
        box(s,29,44,6,8,0xffb3cbd9u); box(s,19,52,26,4,0xffd9e8f0u);
    } else if(app==5) {
        disc(s,32,32,27,0xff9059d4u); disc(s,32,32,23,0xff6e3bb0u);
        for(int x=25;x<44;x++) {
            int half=(44-x)*14/19;
            box(s,x,32-half,1,half*2+1,white);
        }
    } else if(app==8) {
        box(s,6,8,52,48,0xffd5e7f2u); box(s,9,11,46,42,0xff328ad0u);
        disc(s,42,22,6,0xffffdf75u);
        for(int x=12;x<54;x++) {
            int top=x<30?48-(x-12):30+(x-30)/2;
            box(s,x,top,1,51-top,0xff69c59bu);
        }
    } else if(app==9) {
        box(s,5,10,54,44,0xff3b5366u);
        stroke(s,24,24,16,32,2,white); stroke(s,16,32,24,40,2,white);
        stroke(s,40,24,48,32,2,white); stroke(s,48,32,40,40,2,white);
        stroke(s,35,22,29,42,2,blue);
    } else if(app==10) {
        box(s,8,8,48,48,0xff0879c5u);
        stroke(s,42,20,22,20,3,white); stroke(s,22,20,22,32,3,white);
        stroke(s,22,32,42,32,3,white); stroke(s,42,32,42,44,3,white);
        stroke(s,42,44,22,44,3,white);
    } else {
        box(s,13,5,38,54,0xffe2eaf0u);
        box(s,18,16,26,3,0xff8ca9bdu); box(s,18,25,26,3,0xff8ca9bdu);
        box(s,18,34,26,3,0xff8ca9bdu); box(s,18,43,18,3,0xff8ca9bdu);
    }
}
static void build(int app,int slot) {
    int size=sizes[slot],ss=size*2;
    kmemset(scratch,0,sizeof(scratch));
    gpu_surface_t s={.pixels=scratch,.w=ss,.h=ss,.stride=ss};
    draw_vector(&s,app);
    for(int y=0;y<size;y++) for(int x=0;x<size;x++) {
        unsigned a=0,r=0,g=0,b=0;
        for(int yy=0;yy<2;yy++) for(int xx=0;xx<2;xx++) {
            uint32_t p=scratch[(y*2+yy)*ss+x*2+xx]; unsigned pa=GPU_A(p);
            a+=pa; r+=GPU_R(p)*pa; g+=GPU_G(p)*pa; b+=GPU_B(p)*pa;
        }
        cache[app][slot][y*size+x]=a?GPU_RGBA(r/a,g/a,b/a,a/4):0;
    }
    ready[app][slot]=1;
}
void ui_icon(gpu_surface_t *dst,int x,int y,int app,int size) {
    if(!dst||!dst->pixels||size<1||size>64) return;
    if(app<0||app>9) app=10;
    int slot=7;
    for(int i=0;i<8;i++) if(sizes[i]==size) { slot=i; break; }
    if(!ready[app][slot]) build(app,slot);
    int source=sizes[slot];
    for(int py=0;py<size;py++) for(int px=0;px<size;px++)
        gpu_put_pixel(dst,x+px,y+py,cache[app][slot][py*source/size*source+px*source/size]);
}
void ui_icon_nobg(gpu_surface_t *s,int x,int y,int app,int size) { ui_icon(s,x,y,app,size); }
void ui_icon_scaled(gpu_surface_t *s,int x,int y,int app,int size) { ui_icon(s,x,y,app,size); }
