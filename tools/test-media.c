/* Run the exact guest decoder with ASan/UBSan, fake clock and PCM sink. */
#include "../kernel/media.c"
extern int printf(const char *,...);
extern int open(const char *,int,...);
extern long read(int,void *,unsigned long);
extern int close(int);
void *kmalloc(size_t n) { return malloc(n); }
const uint8_t media_demo_data[1]={0},media_demo_end[1]={0};
const uint8_t media_bieber_data[1]={0},media_bieber_end[1]={0};
static uint64_t now;
static unsigned samples,crossings;
static int previous;
static uint8_t input[2*1024*1024];
static uint8_t bieber_buf[12*1024*1024];
void *kmemcpy(void *d,const void *s,size_t n) { uint8_t *a=d; const uint8_t *b=s; while(n--) *a++=*b++; return d; }
int kstrlen(const char *s) { int n=0; while(s[n]) n++; return n; }
uint64_t get_uptime_ms(void) { return now; }
void audio_init(void) {}
int audio_available(void) { return 1; }
int audio_start(unsigned rate) { samples=crossings=0; previous=0; return rate==44100; }
void audio_stop(void) {}
void audio_poll(void) {}
void audio_volume(unsigned value) { (void)value; }
int audio_write(const int16_t *data,unsigned count) {
    for(unsigned i=0;i<count;i++) { int v=data[i*2]; if(previous<0&&v>=0) crossings++; previous=v; }
    samples+=count; return 1;
}
uint64_t audio_frames(void) { return samples; }
void gpu_fill_rect(gpu_surface_t *s,int32_t x,int32_t y,uint32_t w,uint32_t h,uint32_t c) { (void)s;(void)x;(void)y;(void)w;(void)h;(void)c; }
void gpu_draw_rect(gpu_surface_t *s,int32_t x,int32_t y,uint32_t w,uint32_t h,uint32_t c,int th) { (void)s;(void)x;(void)y;(void)w;(void)h;(void)c;(void)th; }
void ui_text(gpu_surface_t *s,int x,int y,const char *p,uint32_t c,int z) { (void)s;(void)x;(void)y;(void)p;(void)c;(void)z; }
int ui_text_measure(const char *p,int z) { (void)z; return kstrlen(p)*8; }
void ui_toggle_maximize(int app) { (void)app; }
m_vfs_node_t *fs_lookup(const char *path) { (void)path; return NULL; }
const uint8_t *fs_node_data(const m_vfs_node_t *node) { (void)node; return NULL; }
#define CHECK(x) do { if(!(x)) { printf("FAIL media line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    int fd=open("assets/media/demo.mpg",0); CHECK(fd>=0);
    long size=read(fd,input,sizeof(input)); close(fd); CHECK(size>0);
    CHECK(media_load(input,size,"test"));
    for(unsigned i=0;i<230;i++) { now+=20; media_poll(); }
    CHECK(media_video_frames()==100); CHECK(samples>=176400&&samples<=179000);
    CHECK(crossings>1700&&crossings<1850);
    printf("PASS media: 100 native decoded frames, %u stereo PCM frames, %u tone crossings.\n",samples,crossings);

    /* Test Justin Bieber video (8.7 MB, 640x360 with embedded sequence headers) */
    int bfd = open("assets/media/bieber.mpg", 0);
    if (bfd >= 0) {
        long bsize = read(bfd, bieber_buf, sizeof(bieber_buf));
        close(bfd);
        CHECK(bsize > 1000000);
        CHECK(media_load(bieber_buf, bsize, "Justin Bieber - Off My Face"));
        unsigned initial_samples = samples;
        for (unsigned i = 0; i < 50; i++) { now += 25; media_poll(); }
        CHECK(media_video_frames() > 10);
        CHECK(samples > initial_samples);
        printf("PASS media: Justin Bieber live video decoded %u frames, PCM audio verified.\n", media_video_frames());
    }

    CHECK(!media_load(input,0,"empty")); CHECK(!media_load(input,MEDIA_MAX_FILE+1,"oversize"));
    uint8_t original=input[0]; input[0]=9; CHECK(!media_load(input,size,"bad signature")); input[0]=original;
    for(unsigned i=0;i+8<(unsigned)size;i++) if(!input[i]&&!input[i+1]&&input[i+2]==1&&input[i+3]==0xb3) {
        original=input[i+4]; input[i+4]=255; CHECK(!media_load(input,size,"oversize dimensions")); input[i+4]=original; break;
    }
    for(unsigned n=128;n<50000;n+=433) {
        media_load(input,n,"truncated");
        for(unsigned i=0;i<10;i++) { now+=20; media_poll(); }
    }
    unsigned seed=42;
    for(unsigned trial=0;trial<200;trial++) {
        seed=seed*1664525u+1013904223u;
        unsigned at=seed%50000; uint8_t saved=input[at]; input[at]^=seed>>24;
        media_load(input,50000,"damaged");
        for(unsigned i=0;i<20;i++) { now+=20; media_poll(); }
        input[at]=saved;
    }
    printf("PASS media: invalid signature, size/dimension limits, truncated streams under ASan/UBSan.\n");
    /* The UI scaler must not corrupt memory on clipping or >1080px targets. */
    uint32_t source[16]; for(int i=0;i<16;i++) source[i]=0xff112233u;
    gpu_surface_t surface={.pixels=malloc(2000*1200*4),.w=2000,.h=1200,.stride=2000};
    video_blit_scaled(&surface,0,0,2000,1200,source,4,4);
    CHECK(surface.pixels[1999+1199*2000]==0xff112233u);
    video_blit_scaled(&surface,-1,0,2000,1200,source,4,4);
    video_blit_scaled(&surface,1,1,2000,1200,source,4,4);
    CHECK(media_hit(78,364,600,400)==0);
    CHECK(media_hit(438,364,600,400)==5);
    CHECK(media_hit(20,80,1000,600)==100);
    char long_title[300]; for(int i=0;i<299;i++) long_title[i]='x'; long_title[299]=0;
    CHECK(media_load(input,size,long_title)); CHECK(kstrlen(media_title())==95);
    printf("PASS media: bounded title, large scaler, clipping and control geometry.\n");
    return 0;
}
