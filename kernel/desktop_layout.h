#ifndef SHENLONG_DESKTOP_LAYOUT_H
#define SHENLONG_DESKTOP_LAYOUT_H

/* One source of geometry for painting and pointer hit-testing. */
typedef struct { int x,y,w,h; } desktop_rect_t;
typedef struct {
    desktop_rect_t start,copilot,search,apps[6],network,clock,peek;
    int menu_w,menu_h,menu_x,menu_y;
} desktop_layout_t;
static inline int desktop_contains(desktop_rect_t r,int x,int y) {
    return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;
}
static inline desktop_layout_t desktop_layout(int w,int h) {
    desktop_layout_t l={0};
    l.start=(desktop_rect_t){0,0,48,48};
    l.copilot=(desktop_rect_t){48,0,44,48};
    l.search=(desktop_rect_t){0,0,0,0};
    for(int i=0;i<6;i++) l.apps[i]=(desktop_rect_t){94+i*48,0,48,48};
    l.network=(desktop_rect_t){w-164,0,44,48};
    l.clock=(desktop_rect_t){w-120,0,112,48};
    l.peek=(desktop_rect_t){w-8,0,8,48};
    l.menu_w=w>680?660:w-20;
    l.menu_h=h>640?548:h-56;
    l.menu_x=0; l.menu_y=h-48-l.menu_h;
    return l;
}
static inline desktop_rect_t desktop_app_row(int i,int h) {
    int step=(h-136)/6;
    if(step>54) step=54;
    return (desktop_rect_t){60,54+i*step,214,step-4};
}
static inline desktop_rect_t desktop_tile(int i,int w,int h) {
    int tw=(w-312)/2, th=(h-152)/3;
    if(tw>180) tw=180;
    if(th>126) th=126;
    return (desktop_rect_t){292+(i%2)*(tw+10),54+(i/2)*(th+10),tw,th};
}
static inline desktop_rect_t desktop_control(int width,int action) {
    return (desktop_rect_t){width-139+46*action,1,46,42};
}
#endif
