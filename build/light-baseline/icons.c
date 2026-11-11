/* Original geometric icon family, rendered at 4x and cached per display size. */
#include "icons.h"
#include "theme.h"
static const int sizes[]={20,22,24,32,40,48,56,64};
static uint32_t cache[6][8][64*64],scratch[256*256];
static uint8_t ready[6][8];
static gpu_surface_t work;
static int scale(int v) { return v*(int)work.w/64; }
static void box(int x,int y,int w,int h,uint32_t color) {
    gpu_fill_rect(&work,scale(x),scale(y),scale(w),scale(h),color);
}
static void dot(int x,int y,int r,uint32_t color) {
    x=scale(x); y=scale(y); r=scale(r); if(r<1) r=1;
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++)
        if(dx*dx+dy*dy<=r*r) gpu_put_pixel(&work,x+dx,y+dy,color);
}
static void line(int x0,int y0,int x1,int y1,int thick,uint32_t color) {
    int a=scale(x0),b=scale(y0),c=scale(x1),d=scale(y1),r=scale(thick)/2;
    if(r<1) r=1;
    for(int y=-r;y<=r;y++) for(int x=-r;x<=r;x++) if(x*x+y*y<=r*r)
        gpu_draw_line(&work,a+x,b+y,c+x,d+y,color);
    dot(x0,y0,thick/2,color); dot(x1,y1,thick/2,color);
}
static void ellipse(int cx,int cy,int rx,int ry,int thick,uint32_t color) {
    int x=scale(cx),y=scale(cy),a=scale(rx),b=scale(ry),t=scale(thick);
    int ia=a-t,ib=b-t; if(ia<1||ib<1) return;
    for(int dy=-b;dy<=b;dy++) for(int dx=-a;dx<=a;dx++)
        if((int64_t)dx*dx*b*b+(int64_t)dy*dy*a*a<=(int64_t)a*a*b*b&&
           (int64_t)dx*dx*ib*ib+(int64_t)dy*dy*ia*ia>=(int64_t)ia*ia*ib*ib)
            gpu_put_pixel(&work,x+dx,y+dy,color);
}
static void render(int app,int size,uint32_t *out) {
    work=(gpu_surface_t){.pixels=scratch,.w=size*4,.h=size*4,.stride=size*4};
    kmemset(scratch,0,sizeof(scratch));
    int n=size*4,r=n/4;
    for(int y=1;y<n-1;y++) for(int x=1;x<n-1;x++) {
        int dx=x<r?r-x:(x>=n-r?x-(n-r-1):0),dy=y<r?r-y:(y>=n-r?y-(n-r-1):0);
        if(dx*dx+dy*dy>r*r) continue;
        int v=30-y*8/n;
        int edge=dx*dx+dy*dy>(r-2)*(r-2)||x==1||y==1||x==n-2||y==n-2;
        gpu_put_pixel(&work,x,y,edge?0xff41484du:GPU_RGB(v,v+4,v+6));
    }
    static const uint32_t accents[]={0xff8ddac5,0xffdcc39b,0xffb4a5df,0xff93c9ed,0xff9ad5bf,UI_ACCENT};
    uint32_t c=accents[app];
    if(app==0) {
        line(18,22,28,32,3,c); line(28,32,18,42,3,c); line(34,42,47,42,3,c);
    } else if(app==1) {
        line(15,22,15,44,3,c); line(15,44,49,44,3,c); line(49,44,49,25,3,c);
        line(49,25,32,25,3,c); line(32,25,27,20,3,c); line(27,20,15,20,3,c);
        line(18,29,46,29,2,0xff7b735fu);
    } else if(app==2) {
        for(int i=0;i<3;i++) line(20+i*12,17,20+i*12,47,2,c);
        for(int i=0;i<3;i++) { dot(20+i*12,i==1?38:26,5,c); dot(20+i*12,i==1?38:26,2,UI_RAISED); }
    } else if(app==3) {
        ellipse(32,32,19,19,2,c); ellipse(32,32,9,19,2,c);
        line(14,32,50,32,2,c); line(18,23,46,23,1,0xff4b6d85u); line(18,41,46,41,1,0xff4b6d85u);
    } else if(app==4) {
        line(23,25,41,39,3,c); line(23,39,41,25,3,c);
        dot(20,23,7,c); dot(44,23,7,c); dot(20,41,7,c); dot(44,41,7,c);
        dot(20,23,3,UI_RAISED); dot(44,23,3,UI_RAISED); dot(20,41,3,UI_RAISED); dot(44,41,3,UI_RAISED);
    } else {
        /* A cut-stroke S mark, deliberately distinct from a window logo. */
        line(44,18,25,18,4,c); line(25,18,17,29,4,c); line(17,29,41,35,4,c);
        line(41,35,33,46,4,c); line(33,46,16,46,4,c);
        box(43,42,5,5,0xffd3e6dfu);
    }
    for(int y=0;y<size;y++) for(int x=0;x<size;x++) {
        unsigned a=0,red=0,green=0,blue=0;
        for(int dy=0;dy<4;dy++) for(int dx=0;dx<4;dx++) {
            uint32_t p=scratch[(y*4+dy)*n+x*4+dx]; unsigned alpha=GPU_A(p);
            a+=alpha; red+=GPU_R(p)*alpha; green+=GPU_G(p)*alpha; blue+=GPU_B(p)*alpha;
        }
        out[y*size+x]=a?GPU_RGBA(red/a,green/a,blue/a,a/16):0;
    }
}
void ui_icon(gpu_surface_t *dst,int x,int y,int app,int size) {
    if(app<0||app>4) app=5;
    int slot=0; for(int i=0;i<8;i++) if(sizes[i]==size) slot=i;
    size=sizes[slot];
    if(!ready[app][slot]) { render(app,size,cache[app][slot]); ready[app][slot]=1; }
    for(int py=0;py<size;py++) for(int px=0;px<size;px++) gpu_put_pixel(dst,x+px,y+py,cache[app][slot][py*size+px]);
}
