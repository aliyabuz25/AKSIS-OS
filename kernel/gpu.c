/*
 * SHENLONG OS - GPU Compositor
 * Double buffer, alpha compositing, z-order surface sistemi
 */

#include "gpu.h"
#include "kernel.h"
#include "kernel_info.h"
#include "logo.h"
#include "mouse.h"

/* ------------------------------------------------------------------ */
/* Framebuffer globals                                                 */
/* ------------------------------------------------------------------ */
uint8_t  *gpu_fb       = NULL;
uint32_t  gpu_fb_w     = 0;
uint32_t  gpu_fb_h     = 0;
uint32_t  gpu_fb_pitch = 0;
uint32_t  gpu_fb_bpx   = 0;

/* Back buffer — heap'te allocate edilir (mem_init sonrası) */
static uint32_t *back_buf = NULL;

/* ------------------------------------------------------------------ */
/* Surface pool — heap yerine statik pool                             */
/* ------------------------------------------------------------------ */
/* Her surface max 1920x1080 olabilir ama bunları statik tutamayız.
   Bunun yerine her surface kendi pixel bölgesini back_buf'tan AYIRMAZ,
   surface'ler kendi malloc'd piksel bufferlarını taşır.
   Freestanding ortamda kmalloc kullanıyoruz. */

static gpu_surface_t surfaces[GPU_MAX_SURFACES];

/* ------------------------------------------------------------------ */
/* gpu_init                                                           */
/* ------------------------------------------------------------------ */
void gpu_init(void *fb, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp) {
    gpu_fb       = (uint8_t *)fb;
    gpu_fb_w     = w;
    gpu_fb_h     = h;
    gpu_fb_pitch = pitch;
    gpu_fb_bpx   = bpp / 8;

    for (int i = 0; i < GPU_MAX_SURFACES; i++)
        surfaces[i].used = 0;

    /* Back buffer: 1080p HD default — 4K'ya geçis icin 2560x1440'a kadar
       ayirlir; fazlasiz bellek israf etmeden kaliteyi korur. */
    uint32_t total = w * h;
    uint32_t alloc_total = total > (2560 * 1440) ? total : (2560 * 1440);
    back_buf = (uint32_t *)kmalloc(alloc_total * 4);
    if (!back_buf) return; /* heap yoksa direkt fb'ye yaz */
    for (uint32_t i = 0; i < alloc_total; i++) back_buf[i] = 0xFF000000u;
}

/* ------------------------------------------------------------------ */
/* Surface yönetimi                                                   */
/* ------------------------------------------------------------------ */
gpu_surface_t *gpu_surface_create(uint32_t w, uint32_t h, int32_t x, int32_t y, int z) {
    if(!w||!h||(uint64_t)w*h>GPU_SURFACE_BUF/4) return NULL;
    gpu_surface_t *s = NULL;
    for (int i = 0; i < GPU_MAX_SURFACES; i++) {
        if (!surfaces[i].used) { s = &surfaces[i]; break; }
    }
    if (!s) return NULL;

    uint32_t npix = w * h;
    s->pixels  = (uint32_t *)kmalloc(npix * 4);
    if (!s->pixels) return NULL;

    s->w       = w;
    s->h       = h;
    s->x       = x;
    s->y       = y;
    s->stride  = w;
    s->visible = 1;
    s->alpha   = 255;
    s->z       = z;
    s->used    = 1;
    s->opaque = 1;
    s->opaque_inset = 0;
    s->opaque_x0=s->opaque_y0=s->opaque_x1=s->opaque_y1=0;
    return s;
}

void gpu_surface_destroy(gpu_surface_t *s) {
    if (!s || !s->used) return;
    kfree(s->pixels);
    s->pixels = NULL;
    s->used   = 0;
}

void gpu_surface_move(gpu_surface_t *s, int32_t x, int32_t y) {
    if (!s) return;
    s->x = x; s->y = y;
}

void gpu_surface_show(gpu_surface_t *s, int visible) {
    if (s) s->visible = visible;
}

/* ------------------------------------------------------------------ */
/* Surface çizim primitifleri                                         */
/* ------------------------------------------------------------------ */
void gpu_fill(gpu_surface_t *s, uint32_t color) {
    if (!s || !s->pixels) return;
    uint32_t c = color | 0xFF000000u;
    /* Visible width can be smaller than allocation stride after restore. */
    for(uint32_t y=0;y<s->h;y++)
        for(uint32_t x=0;x<s->w;x++) s->pixels[y*s->stride+x]=c;
}

void gpu_fill_rect(gpu_surface_t *s, int32_t x, int32_t y,
                   uint32_t w, uint32_t h, uint32_t color) {
    if (!s) return;
    int32_t x1 = x + (int32_t)w;
    int32_t y1 = y + (int32_t)h;
    if (x1 > (int32_t)s->w) x1 = (int32_t)s->w;
    if (y1 > (int32_t)s->h) y1 = (int32_t)s->h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (GPU_A(color) == 255) {
        for (int32_t py=y; py<y1; py++) {
            uint32_t *dst=s->pixels+(uint32_t)py*s->stride;
            for (int32_t px=x; px<x1; px++) dst[px]=color;
        }
        return;
    }
    for (int32_t py = y; py < y1; py++)
        for (int32_t px = x; px < x1; px++)
            gpu_put_pixel(s, px, py, color);
}

void gpu_gradient_v(gpu_surface_t *s, int32_t x, int32_t y,
                    uint32_t w, uint32_t h, uint32_t top, uint32_t bot) {
    if (!s || h == 0) return;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t t = dy * 255 / (h - 1 > 0 ? h - 1 : 1);
        uint32_t r = (GPU_R(top)*(255-t) + GPU_R(bot)*t) / 255;
        uint32_t g = (GPU_G(top)*(255-t) + GPU_G(bot)*t) / 255;
        uint32_t b = (GPU_B(top)*(255-t) + GPU_B(bot)*t) / 255;
        uint32_t color = GPU_RGB(r, g, b);
        for (uint32_t dx = 0; dx < w; dx++)
            gpu_put_pixel(s, x + (int32_t)dx, y + (int32_t)dy, color);
    }
}

void gpu_draw_rect(gpu_surface_t *s, int32_t x, int32_t y,
                   uint32_t w, uint32_t h, uint32_t color, int thick) {
    if (!s) return;
    for (int t = 0; t < thick; t++) {
        for (uint32_t i = 0; i < w; i++) {
            gpu_put_pixel(s, x+(int32_t)i,         y+t,              color);
            gpu_put_pixel(s, x+(int32_t)i,         y+(int32_t)h-1-t, color);
        }
        for (uint32_t i = 0; i < h; i++) {
            gpu_put_pixel(s, x+t,              y+(int32_t)i,         color);
            gpu_put_pixel(s, x+(int32_t)w-1-t, y+(int32_t)i,         color);
        }
    }
}

void gpu_draw_line(gpu_surface_t *s, int32_t x0, int32_t y0,
                   int32_t x1, int32_t y1, uint32_t color) {
    int32_t dx = x1>x0 ? x1-x0 : x0-x1;
    int32_t dy = y1>y0 ? y1-y0 : y0-y1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    for (;;) {
        gpu_put_pixel(s, x0, y0, color);
        if (x0==x1 && y0==y1) break;
        int32_t e2 = 2*err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void gpu_blit(gpu_surface_t *dst, int32_t dx, int32_t dy,
              const gpu_surface_t *src, int32_t sx, int32_t sy,
              uint32_t w, uint32_t h) {
    if (!dst || !src) return;
    for (uint32_t row = 0; row < h; row++) {
        int32_t sy2 = sy + (int32_t)row;
        int32_t dy2 = dy + (int32_t)row;
        if (sy2 < 0 || (uint32_t)sy2 >= src->h) continue;
        if (dy2 < 0 || (uint32_t)dy2 >= dst->h) continue;
        for (uint32_t col = 0; col < w; col++) {
            int32_t sx2 = sx + (int32_t)col;
            int32_t dx2 = dx + (int32_t)col;
            if (sx2 < 0 || (uint32_t)sx2 >= src->w) continue;
            uint32_t spix = src->pixels[(uint32_t)sy2 * src->stride + (uint32_t)sx2];
            gpu_put_pixel(dst, dx2, dy2, spix);
        }
    }
}

void gpu_blit_logo(gpu_surface_t *dst, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!dst) return;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t sy = (uint32_t)((uint64_t)dy * LOGO_H / h);
        if (sy >= (uint32_t)LOGO_H) sy = LOGO_H - 1;
        for (uint32_t dx = 0; dx < w; dx++) {
            uint32_t sx = (uint32_t)((uint64_t)dx * LOGO_W / w);
            if (sx >= (uint32_t)LOGO_W) sx = LOGO_W - 1;
            uint32_t idx = (sy * (uint32_t)LOGO_W + sx) * 3;
            uint8_t r = logo_rgb[idx];
            uint8_t g = logo_rgb[idx+1];
            uint8_t b = logo_rgb[idx+2];
            gpu_put_pixel(dst, x+(int32_t)dx, y+(int32_t)dy, GPU_RGB(r,g,b));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Compositor — z-order sort (bubble sort, max 16 surface)            */
/* ------------------------------------------------------------------ */
/* Retained compositor: coalesced damage survives frame pacing. */
typedef struct { int x0,y0,x1,y1; } damage_t;
#define MAX_DAMAGE 64
static damage_t damage[MAX_DAMAGE];
static int damage_count;
static gpu_surface_t previous[GPU_MAX_SURFACES];
static uint64_t last_present;
static gpu_stats_t stats;

/* CPU presentation budget, not a claim about the physical display refresh. */
#define GPU_TARGET_FPS 100ULL

static inline uint64_t gpu_rdtsc(void) {
    uint32_t lo,hi;
    __asm__ volatile("rdtsc" : "=a"(lo),"=d"(hi));
    return ((uint64_t)hi<<32)|lo;
}
static int max_i(int a,int b) { return a>b?a:b; }
static int min_i(int a,int b) { return a<b?a:b; }

void gpu_damage(int x,int y,int w,int h) {
    if(w<=0||h<=0) return;
    damage_t r={max_i(x,0),max_i(y,0),min_i(x+w,gpu_fb_w),min_i(y+h,gpu_fb_h)};
    if(r.x0>=r.x1||r.y0>=r.y1) return;
    for(int i=0;i<damage_count;) {
        damage_t p=damage[i];
        if(r.x0<=p.x1&&r.x1>=p.x0&&r.y0<=p.y1&&r.y1>=p.y0) {
            r.x0=min_i(r.x0,p.x0); r.y0=min_i(r.y0,p.y0);
            r.x1=max_i(r.x1,p.x1); r.y1=max_i(r.y1,p.y1);
            damage[i]=damage[--damage_count]; i=0;
        } else i++;
    }
    if(damage_count>=MAX_DAMAGE) {
        damage[0].x0=min_i(damage[0].x0,r.x0);
        damage[0].y0=min_i(damage[0].y0,r.y0);
        damage[0].x1=max_i(damage[0].x1,r.x1);
        damage[0].y1=max_i(damage[0].y1,r.y1);
        return;
    }
    damage[damage_count++]=r;
}
void gpu_surface_damage(gpu_surface_t *s,int x,int y,int w,int h) {
    if(!s||!s->visible) return;
    int x1=min_i(x+w,s->w), y1=min_i(y+h,s->h);
    x=max_i(x,0); y=max_i(y,0);
    gpu_damage(s->x+x,s->y+y,x1-x,y1-y);
}
void gpu_surface_alpha(gpu_surface_t *s,int alpha) {
    if(s) s->alpha=max_i(0,min_i(255,alpha));
}
/* The existing wallpaper surface IS the immutable cache; no duplicate buffer. */
void gpu_wallpaper_bake(void) { gpu_damage(0,0,gpu_fb_w,gpu_fb_h); }
void gpu_stats_read(gpu_stats_t *out) { *out=stats; }
void gpu_stats_reset(void) { kmemset(&stats,0,sizeof(stats)); }

/* Back-buffer kopyalama — framebuffer'a cokluk. 4K'da bellek bant
   genisligi baskasidur. 64-bit (4-piksel) unaligned transfer, QEMU
   ramfb/BGA LFB uzerinde 32-bit'e gore 4x hizli ve portable. */
static void copy_pixels(uint32_t *dst,const uint32_t *src,unsigned count) {
    typedef uint64_t unaligned_word __attribute__((aligned(1),may_alias));
    unsigned x=0;
    for(;x+4<=count;x+=4) {
        *(unaligned_word *)(dst+x)=*(const unaligned_word *)(src+x);
        *(unaligned_word *)(dst+x+2)=*(const unaligned_word *)(src+x+2);
    }
    for(;x<count;x++) dst[x]=src[x];
}
static void blend_pixels(uint32_t *dst,const uint32_t *src,unsigned n,unsigned ga) {
    for(unsigned x=0;x<n;x++) {
        uint32_t p=src[x]; unsigned a=GPU_A(p);
        if(ga!=255) a=a*ga/255;
        if(a==255) dst[x]=p;
        else if(a) dst[x]=gpu_alpha_blend(dst[x],(p&0xffffff)|(a<<24));
    }
}
static damage_t opaque_bounds(const gpu_surface_t *s) {
    if(s->opaque) return (damage_t){s->x,s->y,s->x+(int)s->w,s->y+(int)s->h};
    if(s->opaque_x1>s->opaque_x0 && s->opaque_y1>s->opaque_y0)
        return (damage_t){s->x+s->opaque_x0,s->y+s->opaque_y0,
                         s->x+s->opaque_x1,s->y+s->opaque_y1};
    int m=s->opaque_inset;
    if(m>0) return (damage_t){s->x+m,s->y+m,s->x+(int)s->w-m,s->y+(int)s->h-m};
    return (damage_t){0,0,0,0};
}
static void compose_region(const gpu_surface_t *s,damage_t r) {
    int x0=max_i(r.x0,s->x), y0=max_i(r.y0,s->y);
    int x1=min_i(r.x1,s->x+(int)s->w), y1=min_i(r.y1,s->y+(int)s->h);
    if(x0>=x1||y0>=y1) return;
    damage_t opaque=opaque_bounds(s);
    for(int y=y0;y<y1;y++) {
        const uint32_t *src=s->pixels+(y-s->y)*s->stride+x0-s->x;
        uint32_t *dst=back_buf+y*gpu_fb_w+x0;
        unsigned width=x1-x0;
        if(s->alpha==255 && s->opaque) { copy_pixels(dst,src,width); continue; }
        if(s->alpha==255 && y>=opaque.y0 && y<opaque.y1) {
            int a=max_i(x0,opaque.x0), b=min_i(x1,opaque.x1);
            if(a<b) {
                blend_pixels(dst,src,a-x0,255);
                copy_pixels(dst+a-x0,src+a-x0,b-a);
                blend_pixels(dst+b-x0,src+b-x0,x1-b,255);
                continue;
            }
        }
        blend_pixels(dst,src,width,s->alpha);
    }
}
static void compose_visible(damage_t r,const int *order,int count,int from) {
    if(r.x0>=r.x1||r.y0>=r.y1) return;
    /* Partition at opaque interiors. During dragging, wallpaper and window
       decorations behind the terminal no longer consume memory bandwidth. */
    for(int i=from;i>=0;i--) {
        const gpu_surface_t *s=&surfaces[order[i]];
        if(s->alpha!=255) continue;
        damage_t bounds=opaque_bounds(s);
        damage_t c={max_i(r.x0,bounds.x0),max_i(r.y0,bounds.y0),
                    min_i(r.x1,bounds.x1),min_i(r.y1,bounds.y1)};
        if(c.x0>=c.x1||c.y0>=c.y1) continue;
        for(int j=i;j<count;j++) compose_region(&surfaces[order[j]],c);
        compose_visible((damage_t){r.x0,r.y0,r.x1,c.y0},order,count,i-1);
        compose_visible((damage_t){r.x0,c.y1,r.x1,r.y1},order,count,i-1);
        compose_visible((damage_t){r.x0,c.y0,c.x0,c.y1},order,count,i-1);
        compose_visible((damage_t){c.x1,c.y0,r.x1,c.y1},order,count,i-1);
        return;
    }
    for(int y=r.y0;y<r.y1;y++) for(int x=r.x0;x<r.x1;x++) back_buf[y*gpu_fb_w+x]=GPU_COL_DESKTOP;
    for(int i=0;i<count;i++) compose_region(&surfaces[order[i]],r);
}
/* ------------------------------------------------------------------ */
/* Buzlu cam efekti                                                   */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Buzlu cam — z=0 wallpaper surface'i blur+tint ile sample et       */
/* ------------------------------------------------------------------ */
void gpu_frost_rect(gpu_surface_t *s, int32_t x, int32_t y,
                    uint32_t w, uint32_t h, uint32_t tint, int radius) {
    if (!s) return;
    /* Wallpaper surface'i bul (z=0) */
    gpu_surface_t *wall = NULL;
    for (int i = 0; i < GPU_MAX_SURFACES; i++) {
        if (surfaces[i].used && surfaces[i].visible && surfaces[i].z == 0) {
            wall = &surfaces[i]; break;
        }
    }
    if (!wall) {
        /* Wallpaper yok — sade tint dolgu yap */
        for (int32_t py=0; py<(int32_t)h; py++)
            for (int32_t px=0; px<(int32_t)w; px++)
                gpu_put_pixel(s, x+px, y+py, tint | 0xFF000000u);
        return;
    }
    if (radius < 1) radius = 1;
    if (radius > 3) radius = 3; /* max 3 — performans */

    int32_t sx0 = s->x + x; /* ekran koordinatı */
    int32_t sy0 = s->y + y;
    uint8_t tr=GPU_R(tint), tg=GPU_G(tint), tb=GPU_B(tint), ta=GPU_A(tint);

    for (int32_t py=0; py<(int32_t)h; py++) {
        int32_t ey = sy0 + py;
        for (int32_t px=0; px<(int32_t)w; px++) {
            int32_t ex = sx0 + px;
            /* Box blur wallpaper'dan */
            uint32_t rr=0,rg=0,rb=0,cnt=0;
            for (int dy2=-radius; dy2<=radius; dy2+=1) {
                int32_t wy = ey+dy2 - wall->y;
                if (wy<0) wy=0;
                if (wy>=(int32_t)wall->h) wy=(int32_t)wall->h-1;
                for (int dx2=-radius; dx2<=radius; dx2+=1) {
                    int32_t wx = ex+dx2 - wall->x;
                    if (wx<0) wx=0;
                    if (wx>=(int32_t)wall->w) wx=(int32_t)wall->w-1;
                    uint32_t wp = wall->pixels[(uint32_t)wy*wall->stride+(uint32_t)wx];
                    rr+=GPU_R(wp); rg+=GPU_G(wp); rb+=GPU_B(wp); cnt++;
                }
            }
            if (cnt) { rr/=cnt; rg/=cnt; rb/=cnt; }
            /* Tint blend */
            if (ta>0) {
                rr=(rr*(255-ta)+(uint32_t)tr*ta)/255;
                rg=(rg*(255-ta)+(uint32_t)tg*ta)/255;
                rb=(rb*(255-ta)+(uint32_t)tb*ta)/255;
            }
            gpu_put_pixel(s, x+px, y+py,
                GPU_RGBA((uint8_t)rr,(uint8_t)rg,(uint8_t)rb,0xFF));
        }
    }
}

void gpu_compose(void) {
    if(!back_buf||!gpu_fb) return;
    uint64_t start=gpu_rdtsc();
    /* Keep damage queued until the next presentation budget. The budget is
       only skipped when pacing is enabled (tsc_freq set AND a prior
       present exists). Test harnesses with tsc_freq=0 bypass pacing
       entirely — damage is always presented. */
    if(last_present&&g_sysinfo.tsc_freq_hz>=1000 &&
       start-last_present<g_sysinfo.tsc_freq_hz/GPU_TARGET_FPS) return;

    int order[GPU_MAX_SURFACES],count=0;
    /* Track even direct geometry changes; preserve old bounds until damaged. */
    for(int i=0;i<GPU_MAX_SURFACES;i++) {
        gpu_surface_t *s=&surfaces[i],*p=&previous[i];
        if(s->used!=p->used||s->visible!=p->visible||s->x!=p->x||s->y!=p->y||
           s->w!=p->w||s->h!=p->h||s->z!=p->z||s->alpha!=p->alpha) {
            if(p->used&&p->visible) gpu_damage(p->x,p->y,p->w,p->h);
            if(s->used&&s->visible) gpu_damage(s->x,s->y,s->w,s->h);
            *p=*s;
        }
        if(s->used&&s->visible&&s->alpha) order[count++]=i;
    }
    if(!damage_count) return;
    for(int i=1;i<count;i++) {
        int key=order[i],j=i-1;
        while(j>=0&&surfaces[order[j]].z>surfaces[key].z) { order[j+1]=order[j]; j--; }
        order[j+1]=key;
    }
    uint64_t pixels=0;
    for(int d=0;d<damage_count;d++) {
        damage_t r=damage[d];
        compose_visible(r,order,count,count-1);
        for(int y=r.y0;y<r.y1;y++) {
            const uint32_t *src=back_buf+y*gpu_fb_w+r.x0;
            uint8_t *dst=gpu_fb+(size_t)y*gpu_fb_pitch+(size_t)r.x0*gpu_fb_bpx;
            if(gpu_fb_bpx==4) copy_pixels((uint32_t *)dst,src,r.x1-r.x0);
            else for(int x=0;x<r.x1-r.x0;x++) {
                dst[x*3]=GPU_B(src[x]); dst[x*3+1]=GPU_G(src[x]); dst[x*3+2]=GPU_R(src[x]);
            }
        }
        pixels+=(uint64_t)(r.x1-r.x0)*(r.y1-r.y0);
    }
    damage_count=0;
    last_present=start;
    uint64_t ticks=gpu_rdtsc()-start;
    stats.frames++; stats.pixels+=pixels; stats.ticks+=ticks;
    if(ticks>stats.max_ticks) stats.max_ticks=ticks;
    if(pixels>stats.max_pixels) stats.max_pixels=pixels;
}
