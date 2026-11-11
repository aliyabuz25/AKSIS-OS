#ifndef SHENLONG_UI_PAINT_H
#define SHENLONG_UI_PAINT_H
#include "gpu.h"
#include "ui_font.h"

/* Allocation-free, shared UI primitives. All geometry is surface-local. */
static inline void ui_round(gpu_surface_t *s,int x,int y,int w,int h,int r,uint32_t c) {
    if(!s || w<=0 || h<=0) return;
    if(r<0) r=0; if(r>w/2) r=w/2; if(r>h/2) r=h/2;
    gpu_fill_rect(s,x+r,y,w-2*r,h,c);
    gpu_fill_rect(s,x,y+r,r,h-2*r,c);
    gpu_fill_rect(s,x+w-r,y+r,r,h-2*r,c);
    for(int py=0;py<r;py++) for(int px=0;px<r;px++) {
        unsigned cov=0;
        for(int sy=0;sy<4;sy++) for(int sx=0;sx<4;sx++) {
            int dx=(r-px)*8-1-sx*2,dy=(r-py)*8-1-sy*2;
            cov+=dx*dx+dy*dy<=r*r*64;
        }
        uint32_t p=(c&0xffffff)|((GPU_A(c)*cov/16)<<24);
        gpu_put_pixel(s,x+px,y+py,p); gpu_put_pixel(s,x+w-1-px,y+py,p);
        gpu_put_pixel(s,x+px,y+h-1-py,p); gpu_put_pixel(s,x+w-1-px,y+h-1-py,p);
    }
}

static inline void ui_round_top(gpu_surface_t *s, int x, int y, int w, int h, int r, uint32_t c) {
    if (!s || w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h) r = h;
    if (r == 0) { gpu_fill_rect(s, x, y, w, h, c); return; }
    gpu_fill_rect(s, x + r, y, w - 2 * r, r, c);
    gpu_fill_rect(s, x, y + r, w, h - r, c);
    for (int py = 0; py < r; py++) {
        for (int px = 0; px < r; px++) {
            unsigned cov = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int dx = (r - px) * 8 - 1 - sx * 2;
                    int dy = (r - py) * 8 - 1 - sy * 2;
                    cov += (dx * dx + dy * dy <= r * r * 64);
                }
            }
            if (cov) {
                uint32_t a = (GPU_A(c) * cov) / 16;
                uint32_t p = (c & 0x00FFFFFFu) | (a << 24);
                gpu_put_pixel(s, x + px, y + py, p);
                gpu_put_pixel(s, x + w - 1 - px, y + py, p);
            }
        }
    }
}

static inline void ui_round_bottom(gpu_surface_t *s, int x, int y, int w, int h, int r, uint32_t c) {
    if (!s || w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h) r = h;
    if (r == 0) { gpu_fill_rect(s, x, y, w, h, c); return; }
    gpu_fill_rect(s, x, y, w, h - r, c);
    gpu_fill_rect(s, x + r, y + h - r, w - 2 * r, r, c);
    for (int py = 0; py < r; py++) {
        for (int px = 0; px < r; px++) {
            unsigned cov = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int dx = (r - px) * 8 - 1 - sx * 2;
                    int dy = (r - py) * 8 - 1 - sy * 2;
                    cov += (dx * dx + dy * dy <= r * r * 64);
                }
            }
            if (cov) {
                uint32_t a = (GPU_A(c) * cov) / 16;
                uint32_t p = (c & 0x00FFFFFFu) | (a << 24);
                gpu_put_pixel(s, x + px, y + h - 1 - py, p);
                gpu_put_pixel(s, x + w - 1 - px, y + h - 1 - py, p);
            }
        }
    }
}

static inline void ui_mask_bottom_corners(gpu_surface_t *s, int w, int h, int r) {
    if (!s || r <= 0 || w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h) r = h;
    for (int py = 0; py < r; py++) {
        int y = h - r + py;
        if (y < 0 || y >= (int)s->h) continue;
        for (int px = 0; px < r; px++) {
            unsigned cov = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int dx = (r - px) * 8 - 1 - sx * 2;
                    int dy = (r - py) * 8 - 1 - sy * 2;
                    cov += (dx * dx + dy * dy <= r * r * 64);
                }
            }
            int x_left = px;
            int x_right = w - 1 - px;
            if (cov == 0) {
                if (x_left >= 0 && x_left < (int)s->w)
                    s->pixels[y * s->stride + x_left] = 0;
                if (x_right >= 0 && x_right < (int)s->w)
                    s->pixels[y * s->stride + x_right] = 0;
            } else if (cov < 16) {
                if (x_left >= 0 && x_left < (int)s->w) {
                    uint32_t *p0 = &s->pixels[y * s->stride + x_left];
                    uint32_t a0 = (GPU_A(*p0) * cov) / 16;
                    *p0 = (*p0 & 0x00FFFFFFu) | (a0 << 24);
                }
                if (x_right >= 0 && x_right < (int)s->w) {
                    uint32_t *p1 = &s->pixels[y * s->stride + x_right];
                    uint32_t a1 = (GPU_A(*p1) * cov) / 16;
                    *p1 = (*p1 & 0x00FFFFFFu) | (a1 << 24);
                }
            }
        }
    }
}

static inline void ui_round_border_corners(gpu_surface_t *s, int w, int h, int r,
                                           uint32_t col_top, uint32_t col_bot) {
    if (!s || r <= 1 || w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int r2_out = r * r * 64;
    int r2_in  = (r - 1) * (r - 1) * 64;
    for (int py = 0; py < r; py++) {
        for (int px = 0; px < r; px++) {
            unsigned cov_out = 0;
            unsigned cov_in  = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    int dx = (r - px) * 8 - 1 - sx * 2;
                    int dy = (r - py) * 8 - 1 - sy * 2;
                    int dist2 = dx * dx + dy * dy;
                    cov_out += (dist2 <= r2_out);
                    cov_in  += (dist2 <= r2_in);
                }
            }
            if (cov_out > cov_in) {
                unsigned cov = cov_out - cov_in;
                if (cov > 16) cov = 16;
                if (col_top) {
                    uint32_t at = (GPU_A(col_top) * cov) / 16;
                    if (at) {
                        uint32_t pt = (col_top & 0x00FFFFFFu) | (at << 24);
                        gpu_put_pixel(s, px, py, pt);
                        gpu_put_pixel(s, w - 1 - px, py, pt);
                    }
                }
                if (col_bot) {
                    uint32_t ab = (GPU_A(col_bot) * cov) / 16;
                    if (ab) {
                        uint32_t pb = (col_bot & 0x00FFFFFFu) | (ab << 24);
                        gpu_put_pixel(s, px, h - 1 - py, pb);
                        gpu_put_pixel(s, w - 1 - px, h - 1 - py, pb);
                    }
                }
            }
        }
    }
}
static inline void ui_text_fit(gpu_surface_t *s,int x,int y,int width,const char *str,uint32_t c,int size) {
    if(!s || x<0 || y<0 || x>=(int)s->w || y>=(int)s->h || width<=0) return;
    if(width>(int)s->w-x) width=s->w-x;
    gpu_surface_t clip=*s;
    clip.pixels=s->pixels+y*s->stride+x; clip.w=width; clip.h=s->h-y;
    char text[256]; int n=0;
    while(str[n] && n<252) { text[n]=str[n]; n++; } text[n]=0;
    if(ui_text_measure(text,size)>width) {
        do {
            if(!n) break;
            do { n--; } while(n && ((unsigned char)text[n]&0xc0)==0x80);
            text[n]=0;
        } while(ui_text_measure(text,size)+ui_text_measure("...",size)>width);
        text[n++]='.'; text[n++]='.'; text[n++]='.'; text[n]=0;
    }
    ui_text(&clip,0,0,text,c,size);
}
static inline int ui_ease(unsigned elapsed,unsigned duration) {
    if(elapsed>=duration) return 256;
    unsigned t=256-elapsed*256/duration;
    return 256-(t*t*t>>16);
}
#endif
