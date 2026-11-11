#ifndef SHENLONG_DESKTOP_LAYOUT_H
#define SHENLONG_DESKTOP_LAYOUT_H

/* Single source of geometry for paint and pointer hit-testing. */
typedef struct { int x,y,w,h; } desktop_rect_t;
typedef struct {
    desktop_rect_t start,copilot,search,apps[6],network,clock,peek;
    int menu_w,menu_h,menu_x,menu_y;
} desktop_layout_t;
static inline int desktop_contains(desktop_rect_t r,int x,int y) {
    return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;
}
/* 4K/1440p scaled layout — 8-bit fixed-point scale (256=1.0, 384=1.5,
   512=2.0). Dock height, icon width, and menu size all grow with this
   factor; at 1x (1080p) the values match the originals. */
static inline desktop_layout_t desktop_layout_scaled(int w,int h,int scale) {
    #define SP(n) ((int)(((uint32_t)(n) * (uint32_t)(scale)) >> 8))
    desktop_layout_t l={0};
    int dh = SP(48);
    l.start=(desktop_rect_t){0,0,dh,dh};
    l.copilot=(desktop_rect_t){dh,0,SP(44),dh};
    l.search=(desktop_rect_t){0,0,0,0};
    for(int i=0;i<6;i++) l.apps[i]=(desktop_rect_t){SP(94)+i*dh,0,dh,dh};
    l.network=(desktop_rect_t){w-SP(164),0,SP(44),dh};
    l.clock=(desktop_rect_t){w-SP(120),0,SP(112),dh};
    l.peek=(desktop_rect_t){w-SP(8),0,SP(8),dh};
    l.menu_w=w>680?SP(660):w-20;
    l.menu_h=h>640?SP(548):h-56;
    l.menu_x=0; l.menu_y=h-dh-l.menu_h;
    #undef SP
    return l;
}
static inline desktop_layout_t desktop_layout(int w,int h) {
    return desktop_layout_scaled(w, h, 256);
}
/* 8-bit fixed-point scaled app rows and tile layout.
   At 1x (scale=256) behavior is unchanged. */
static inline desktop_rect_t desktop_app_row_scaled(int i,int h,int scale) {
    int step1x=(h-136)/6;
    if(step1x>54) step1x=54;
    int step=(step1x*scale)>>8;
    int y0=(54*scale)>>8, x0=(60*scale)>>8, rw=(214*scale)>>8;
    int rh=step-((4*scale)>>8);
    return (desktop_rect_t){x0,y0+i*step,rw,rh};
}
static inline desktop_rect_t desktop_app_row(int i,int h) {
    return desktop_app_row_scaled(i,h,256);
}
static inline desktop_rect_t desktop_tile_scaled(int i,int w,int h,int scale) {
    int tw1x=(w-312)/2, th1x=(h-152)/3;
    if(tw1x>180) tw1x=180;
    if(th1x>126) th1x=126;
    int tw=(tw1x*scale)>>8, th=(th1x*scale)>>8;
    int gap=(10*scale)>>8;
    int x0=(292*scale)>>8;
    if(i%2) x0+=tw+gap;
    int y0=((54*scale)>>8)+((i/2)*(th+gap));
    return (desktop_rect_t){x0,y0,tw,th};
}
static inline desktop_rect_t desktop_tile(int i,int w,int h) {
    return desktop_tile_scaled(i,w,h,256);
}
static inline desktop_rect_t desktop_control(int width,int action) {
    return (desktop_rect_t){width-139+46*action,1,46,42};
}
#endif
