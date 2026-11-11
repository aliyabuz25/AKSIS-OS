/* Compare dirty composition against an independent full-frame reference. */
#include "../kernel/gpu.h"
#include "../kernel/kernel_info.h"
extern void *malloc(size_t);
extern int puts(const char *);
extern int printf(const char *, ...);
sysinfo_t g_sysinfo;
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { (void)p; }
void *kmemset(void *p,int c,size_t n) { uint8_t *d=p; while(n--) *d++=c; return p; }
static gpu_surface_t *layers[4];
static uint8_t *screen;
static int bpx,pitch;
static uint32_t reference_blend(uint32_t dst,uint32_t src,int global) {
    unsigned a=(src>>24)*global/255;
    unsigned r=(((src>>16)&255)*a+((dst>>16)&255)*(255-a))/255;
    unsigned g=(((src>>8)&255)*a+((dst>>8)&255)*(255-a))/255;
    unsigned b=((src&255)*a+(dst&255)*(255-a))/255;
    return 0xff000000|(r<<16)|(g<<8)|b;
}
static int verify(void) {
    for(int y=0;y<200;y++) for(int x=0;x<320;x++) {
        uint32_t value=GPU_COL_DESKTOP;
        for(int z=0;z<4;z++) {
            gpu_surface_t *s=layers[z];
            if(!s->visible||x<s->x||y<s->y||x>=s->x+(int)s->w||y>=s->y+(int)s->h) continue;
            uint32_t p=s->pixels[(y-s->y)*s->stride+x-s->x];
            value=reference_blend(value,p,s->alpha);
        }
        uint8_t *p=screen+y*pitch+x*bpx;
        if(p[0]!=GPU_B(value)||p[1]!=GPU_G(value)||p[2]!=GPU_R(value)) return 0;
    }
    for(int y=0;y<200;y++) for(int p=320*bpx;p<pitch;p++) if(screen[y*pitch+p]!=0xa5) return 0;
    return 1;
}
int main(int argc,char **argv) {
    (void)argv;
    /* Restore keeps a fullscreen allocation stride. Filling the visible
       rectangle must neither skip rows nor overwrite the hidden columns. */
    uint32_t storage[40];
    for(int i=0;i<40;i++) storage[i]=0x12345678;
    gpu_surface_t view={.pixels=storage,.w=3,.h=4,.stride=10};
    gpu_fill(&view,0xffabcdef);
    for(int y=0;y<4;y++) for(int x=0;x<10;x++)
        if(storage[y*10+x]!=(x<3?0xffabcdefu:0x12345678u)) return 15;
    if(gpu_surface_create(0,1,0,0,0)||gpu_surface_create(0xffffffffu,2,0,0,0)) return 16;
    uint32_t pixel=0;
    gpu_surface_t transparent={.pixels=&pixel,.w=1,.h=1,.stride=1};
    gpu_put_pixel(&transparent,0,0,0x80ffffff);
    if(pixel!=0x80ffffff) return 13;
    gpu_put_pixel(&transparent,0,0,0x80ffffff);
    if(GPU_A(pixel)!=191||(pixel&0xffffff)!=0xffffff) return 14;
    bpx=argc>1?3:4; pitch=320*bpx+16;
    screen=malloc(pitch*200); kmemset(screen,0xa5,pitch*200);
    gpu_init(screen,320,200,pitch,bpx*8);
    layers[0]=gpu_surface_create(320,200,0,0,0); gpu_fill(layers[0],0xff234567);
    layers[1]=gpu_surface_create(100,80,30,20,1); gpu_fill(layers[1],0xff8899aa);
    layers[1]->opaque=0; layers[1]->opaque_inset=4;
    layers[1]->pixels[0]=0; layers[1]->pixels[1]=0x80ffffff;
    layers[2]=gpu_surface_create(40,40,70,60,2); gpu_fill(layers[2],0xff55bb33);
    layers[2]->alpha=123;
    layers[3]=gpu_surface_create(13,20,90,45,999); layers[3]->opaque=0;
    for(unsigned i=0;i<260;i++) layers[3]->pixels[i]=i%2?0xf0ffffff:0;
    gpu_compose(); if(!verify()) return 1;
    /* Thin opaque body under a translucent title: no symmetric inset shortcut. */
    layers[1]->opaque_inset=0;
    layers[1]->opaque_x0=1; layers[1]->opaque_y0=12;
    layers[1]->opaque_x1=99; layers[1]->opaque_y1=79;
    for(unsigned y=0;y<12;y++) for(unsigned x=0;x<100;x++)
        layers[1]->pixels[y*100+x]=0x99667788u;
    gpu_surface_damage(layers[1],0,0,100,80); gpu_compose();
    if(!verify()) return 12;
    gpu_stats_reset();
    gpu_surface_move(layers[3],100,50); gpu_compose();
    gpu_stats_t stats; gpu_stats_read(&stats);
    if(!verify()||stats.pixels>800||stats.frames!=1) return 2;
    printf("PASS %dbpp: cursor move presents %llu pixels instead of 64000.\n",bpx*8,stats.pixels);
    gpu_compose(); gpu_stats_read(&stats); if(stats.frames!=1) return 3;
    gpu_surface_move(layers[1],-20,150); gpu_compose(); if(!verify()) return 4;
    gpu_surface_show(layers[2],0); gpu_compose(); if(!verify()) return 5;
    for(int a=0;a<=255;a+=17) {
        gpu_surface_alpha(layers[1],a); gpu_compose(); if(!verify()) return 6;
    }
    gpu_surface_show(layers[2],1); gpu_compose(); if(!verify()) return 7;
    layers[0]->pixels[5*320+7]=0xff1122ee;
    gpu_surface_damage(layers[0],7,5,1,1);
    gpu_compose(); if(!verify()) return 8;
    /* A throttled final update must remain queued until the next present. */
    gpu_stats_read(&stats); uint64_t before=stats.frames;
    g_sysinfo.tsc_freq_hz=1000000000000000ULL;
    layers[0]->pixels[8*320+9]=0xffee2211;
    gpu_surface_damage(layers[0],9,8,1,1); gpu_compose();
    gpu_stats_read(&stats); if(stats.frames!=before) return 9;
    g_sysinfo.tsc_freq_hz=0; gpu_compose(); if(!verify()) return 10;
    gpu_stats_read(&stats); if(stats.frames!=before+1) return 11;
    puts("PASS: alpha/transparent corners, precise opaque body, overlap, clipping, hide/show, fade, dirty content, pitch, idle suppression, paced tail frame.");
    return 0;
}
