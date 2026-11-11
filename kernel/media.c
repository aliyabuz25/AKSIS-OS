/*
 * SHENLONG OS - Yüksek Performanslı Medya Oynatıcı Motoru
 * Doğrudan Ekran Sürücüsü Ölçekleyici (DirectVideo Accelerated Scaler)
 * Sıfır gecikme, akıcı kare işleme ve sinematik tek kenarlık
 */

#include "media.h"
#include "audio.h"
#include "kernel_info.h"
#include "ui_font.h"
#include "theme.h"
#include "ui_paint.h"
#include "keyboard.h"

extern void ui_toggle_maximize(int app);

static uint8_t *arena;
#define ARENA_SIZE (128*1024*1024)
static size_t arena_used;
static void *recovery[5];

static void *media_alloc(size_t n) {
    n = ALIGN(n, 16);
    if (!arena || n > ARENA_SIZE - 16 || arena_used > ARENA_SIZE - n - 16) __builtin_longjmp(recovery, 1);
    size_t *p = (size_t *)(arena + arena_used);
    *p = n;
    arena_used += n + 16;
    return (uint8_t *)p + 16;
}

static void *media_realloc(void *p, size_t n) {
    void *next = media_alloc(n);
    if (p) {
        size_t old = *(size_t *)((uint8_t *)p - 16);
        kmemcpy(next, p, old < n ? old : n);
    }
    return next;
}

#define PLM_NO_STDIO
#define PLM_MALLOC(n) media_alloc(n)
#define PLM_REALLOC(p,n) media_realloc(p,n)
#define PLM_FREE(p) ((void)(p))
#define PL_MPEG_IMPLEMENTATION
#include "../vendor/pl_mpeg.h"

#define MEDIA_MAX_FILE (512*1024*1024)
#define MEDIA_MAX_W 1920
#define MEDIA_MAX_H 1080

extern const uint8_t media_demo_data[], media_demo_end[];
extern const uint8_t media_bieber_data[], media_bieber_end[];

static int m_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static void m_strcpy(char *d, const char *s) {
    if (!d || !s) return;
    while ((*d++ = *s++));
}
static void m_strcat(char *d, const char *s) {
    if (!d || !s) return;
    while (*d) d++;
    while ((*d++ = *s++));
}
static int m_strcmp(const char *a, const char *b) {
    if (!a || !b) return a ? 1 : (b ? -1 : 0);
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

/* VFS zayıf referansları */
#define M_MAX_CHILDREN 32
typedef enum { M_NODE_DIR = 1, M_NODE_FILE = 2 } m_vfs_type_t;
typedef struct m_vfs_node {
    char name[64];
    m_vfs_type_t type;
    uint16_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint8_t  data[4096];
    uint8_t *dyn_data;
    uint32_t dyn_cap;
    struct m_vfs_node *parent;
    struct m_vfs_node *children[M_MAX_CHILDREN];
    int child_count;
} m_vfs_node_t;

extern m_vfs_node_t *fs_lookup(const char *path) __attribute__((weak));
extern const uint8_t *fs_node_data(const m_vfs_node_t *node) __attribute__((weak));

#define MAX_PLAYLIST 16
typedef struct {
    char name[64];
    char desc[48];
    int is_builtin; /* 1 = Bieber, 2 = Demo, 0 = File */
    char path[128];
} media_track_t;

static media_track_t playlist[MAX_PLAYLIST];
static int playlist_count = 2;
static int active_track_idx = 0;

static plm_t *decoder;
static uint32_t *pixels;
static int16_t audio_pcm[PLM_AUDIO_SAMPLES_PER_FRAME * 2];
static unsigned width, height, frames;
static int playing, changed, muted;
static uint64_t last_tick;
static const char *message = "Justin Bieber veya ornek videoyu oynatin.";
static char title[96] = "Justin Bieber - Off My Face (Live from Paris)";

static unsigned active_track = 0; /* 0 = Bieber, 1 = Demo */
static unsigned volume_pct = 75;
static int loop_enabled = 0;
static double duration_seconds;
static int media_hover=-1, media_drag;
static uint64_t playlist_refreshed;
static double seek_preview=-1;
static int sidebar_width(int w) { return w>=800?200:0; }

/* Doğrudan Ekran Sürücüsü: Önceden hesaplanmış X ve Y haritalama tabloları */
static uint16_t s_x_map[MEDIA_MAX_W];
static uint16_t s_y_map[MEDIA_MAX_H];
static unsigned s_cached_sw = 0, s_cached_dw = 0;
static unsigned s_cached_sh = 0, s_cached_dh = 0;

static void video(plm_t *plm, plm_frame_t *frame, void *user) {
    (void)plm; (void)user;
    if (frame->width > MEDIA_MAX_W || frame->height > MEDIA_MAX_H) __builtin_longjmp(recovery, 1);
    width = frame->width;
    height = frame->height;
    plm_frame_to_bgra(frame, (uint8_t *)pixels, width * 4);
    frames++;
    changed = 1;
}

static void sound(plm_t *plm, plm_samples_t *samples, void *user) {
    (void)plm; (void)user;
    for (unsigned i = 0; i < samples->count * 2; i++) {
        float value = samples->interleaved[i] * 26000.0f;
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        audio_pcm[i] = (int16_t)value;
    }
    audio_write(audio_pcm, samples->count);
}

static void media_heap_init(void) {
    if (!arena)  arena  = (uint8_t  *)kmalloc(ARENA_SIZE);
    if (!pixels) pixels = (uint32_t *)kmalloc(MEDIA_MAX_W * MEDIA_MAX_H * 4);
}

void media_stop(void) {
    media_heap_init();
    playing = 0;
    audio_stop();
    changed = 1;
    message = "Durduruldu.";
}

int media_load(const void *data, unsigned size, const char *name) {
    media_stop();
    decoder = NULL;
    width = height = frames = 0;
    duration_seconds=0; seek_preview=-1; media_drag=0;
    arena_used = 0;
    s_cached_sw = s_cached_dw = 0;
    s_cached_sh = s_cached_dh = 0;

    if (!data || size < 128 || size > MEDIA_MAX_FILE) {
        message = "Video size not supported.";
        return 0;
    }
    const uint8_t *p = (const uint8_t *)data;
    unsigned sequences = 0;
    if (p[0] || p[1] || p[2] != 1 || p[3] != 0xba) {
        message = "MPEG-1 program stream required (.mpg).";
        return 0;
    }
    for (unsigned i = 0; i + 8 < size; i++) {
        if (!p[i] && !p[i+1] && p[i+2] == 1 && p[i+3] == 0xb3) {
            unsigned w = (p[i+4] << 4) | (p[i+5] >> 4);
            unsigned h = ((p[i+5] & 15) << 8) | p[i+6];
            unsigned rate = p[i+7] & 15;
            if (!w || !h || w > MEDIA_MAX_W || h > MEDIA_MAX_H || !rate || rate > 8) {
                message = "Video dimensions exceed 1920 x 1080.";
                return 0;
            }
            sequences++;
            break;
        }
    }
    if (!sequences) {
        message = "Video header not found.";
        return 0;
    }

    if (__builtin_setjmp(recovery)) {
        decoder = NULL;
        audio_stop();
        message = "Unable to decode video or out of memory.";
        return 0;
    }

    decoder = plm_create_with_memory((uint8_t *)(uintptr_t)data, size, 0);
    if (!decoder || !plm_has_headers(decoder) || !plm_get_num_video_streams(decoder)) {
        decoder = NULL;
        message = "MPEG-1 video track not found.";
        return 0;
    }

    if(!name) name="Video";
    unsigned n = kstrlen(name);
    if (n >= sizeof(title)) n = sizeof(title) - 1;
    kmemcpy(title, name, n);
    title[n] = 0;

    duration_seconds=plm_get_duration(decoder);

    for (unsigned i = 0; i < MEDIA_MAX_W * MEDIA_MAX_H; i++) pixels[i] = 0xff000000;

    plm_set_video_decode_callback(decoder, video, NULL);
    int sound_ok = plm_get_num_audio_streams(decoder) && audio_start(plm_get_samplerate(decoder));
    plm_set_audio_enabled(decoder, sound_ok);
    if (sound_ok) plm_set_audio_decode_callback(decoder, sound, NULL);
    plm_set_audio_lead_time(decoder, 0.15); /* Fluid AV synchronization */
    plm_set_loop(decoder, loop_enabled);
    audio_volume(muted ? 0 : volume_pct);

    last_tick = get_uptime_ms();
    playing = changed = 1;
    message = sound_ok ? "Playing · Stereo Audio" : "Playing · No Audio";
    return 1;
}

static void media_refresh_playlist(void) {
    playlist_count = 0;
    m_strcpy(playlist[0].name, "Justin Bieber");
    m_strcpy(playlist[0].desc, "Off My Face");
    playlist[0].is_builtin = 1;
    playlist[0].path[0] = '\0';
    playlist_count++;

    m_strcpy(playlist[1].name, "SHENLONG Demo");
    m_strcpy(playlist[1].desc, "Sample MPEG Preview");
    playlist[1].is_builtin = 2;
    playlist[1].path[0] = '\0';
    playlist_count++;

    if (fs_lookup) {
        const char *scan_dirs[] = {"/movies", "/videos", "/home/root"};
        for (int d = 0; d < 3; d++) {
            m_vfs_node_t *dir = fs_lookup(scan_dirs[d]);
            if (dir && dir->type == M_NODE_DIR) {
                for (int i = 0; i < dir->child_count && playlist_count < MAX_PLAYLIST; i++) {
                    m_vfs_node_t *child = dir->children[i];
                    if (child && child->type == M_NODE_FILE && child->size > 0) {
                        int clen = m_strlen(child->name);
                        int is_vid = 0;
                        if (clen > 4) {
                            const char *ext = child->name + clen - 4;
                            if (m_strcmp(ext, ".mpg") == 0 || 
                                m_strcmp(ext, ".MPG") == 0 || 
                                m_strcmp(ext, ".m1v") == 0) is_vid = 1;
                        }
                        if (is_vid) {
                            m_strcpy(playlist[playlist_count].name, child->name);
                            if (d == 0) m_strcpy(playlist[playlist_count].desc, "Kalici Disk (/movies)");
                            else if (d == 1) m_strcpy(playlist[playlist_count].desc, "Kalici Disk (/videos)");
                            else m_strcpy(playlist[playlist_count].desc, "Kullanici Dizini");
                            playlist[playlist_count].is_builtin = 0;
                            m_strcpy(playlist[playlist_count].path, scan_dirs[d]);
                            m_strcat(playlist[playlist_count].path, "/");
                            m_strcat(playlist[playlist_count].path, child->name);
                            playlist_count++;
                        }
                    }
                }
            }
        }
    }
}

void media_select_track(int idx) {
    if (idx < 0 || idx >= playlist_count) return;
    active_track_idx = idx;
    if (playlist[idx].is_builtin == 1) {
        media_bieber();
    } else if (playlist[idx].is_builtin == 2) {
        media_demo();
    } else if (playlist[idx].is_builtin == 0) {
        if (fs_lookup && fs_node_data) {
            m_vfs_node_t *node = fs_lookup(playlist[idx].path);
            if (node && node->size > 0) {
                const uint8_t *fdata = fs_node_data(node);
                if (fdata) {
                    media_load(fdata, node->size, playlist[idx].name);
                }
            }
        }
    }
}

void media_demo(void) {
    active_track = 1;
    active_track_idx = 1;
    media_load(media_demo_data, media_demo_end - media_demo_data, "SHENLONG Demo Clip");
}

void media_bieber(void) {
    active_track = 0;
    active_track_idx = 0;
    media_load(media_bieber_data, media_bieber_end - media_bieber_data, "Justin Bieber - Off My Face (Live from Paris)");
}

/* 
 * Yüksek Hassasiyetli Video Yoklama (Driver-Level Fluidity):
 * Süre farkı kayıplarını sıfırlar; kare atlamasını önler ve sesi tam senkron tutar.
 */
int media_poll(void) {
    audio_poll();
    if (playing && decoder) {
        uint64_t now = get_uptime_ms();
        if (last_tick == 0) last_tick = now;
        uint64_t delta = now - last_tick;
        if (delta >= 10) { /* ~100Hz frekansla akıcı yoklama */
            last_tick = now;
            double sec = (double)delta / 1000.0;
            if (sec > 0.08) sec = 0.08; /* Zaman sapmasını telafi et */
            if (__builtin_setjmp(recovery)) {
                media_stop();
                decoder = NULL;
                message = "Video memory limit exceeded.";
            } else {
                plm_decode(decoder, sec);
                if (plm_has_ended(decoder)) {
                    if (loop_enabled) {
                        plm_rewind(decoder);
                    } else {
                        playing = 0;
                        message = "Playback completed.";
                    }
                    changed = 1;
                }
            }
        }
    }
    int result = changed;
    changed = 0;
    return result;
}

/* 
 * DirectVideo Hızlandırılmış Satır Ölçekleyici:
 * - 0 bölme işlemi, 0 döngü içi kaydırma
 * - 8-yollu unrolled bellek yazımı
 * - Donanım veri yolunu doyuran en yüksek hız
 */
static void video_blit_scaled(gpu_surface_t *s, int dx, int dy, unsigned dw, unsigned dh,
                              const uint32_t *src, unsigned sw, unsigned sh) {
    if (dw == 0 || dh == 0 || sw == 0 || sh == 0 || !src || !s) return;
    if(dx<0||dy<0||dx+(uint64_t)dw>s->w||dy+(uint64_t)dh>s->h) return;
    if(dw>MEDIA_MAX_W||dh>MEDIA_MAX_H) {
        for(unsigned yy=0;yy<dh;yy++) for(unsigned xx=0;xx<dw;xx++)
            s->pixels[(dy+yy)*s->stride+dx+xx]=src[(uint64_t)yy*sh/dh*sw+(uint64_t)xx*sw/dw];
        return;
    }

    /* 1:1 Doğrudan Bit: Ölçekleme yoksa doğrudan memcpy ile VRAM aktarımı */
    if (dw == sw && dh == sh) {
        for (unsigned py = 0; py < dh; py++) {
            kmemcpy(s->pixels + (dy + py) * s->stride + dx, src + py * sw, dw * 4);
        }
        return;
    }

    /* X ve Y haritalarını yalnızca boyut veya çözünürlük değiştiğinde 1 kez hesapla */
    if (s_cached_sw != sw || s_cached_dw != dw) {
        uint32_t x_step = ((uint64_t)sw << 16) / dw;
        uint32_t x_acc = 0;
        for (unsigned px = 0; px < dw; px++) {
            uint32_t sx = x_acc >> 16;
            if (sx >= sw) sx = sw - 1;
            s_x_map[px] = (uint16_t)sx;
            x_acc += x_step;
        }
        s_cached_sw = sw;
        s_cached_dw = dw;
    }

    if (s_cached_sh != sh || s_cached_dh != dh) {
        uint32_t y_step = ((uint64_t)sh << 16) / dh;
        uint32_t y_acc = 0;
        for (unsigned py = 0; py < dh; py++) {
            uint32_t sy = y_acc >> 16;
            if (sy >= sh) sy = sh - 1;
            s_y_map[py] = (uint16_t)sy;
            y_acc += y_step;
        }
        s_cached_sh = sh;
        s_cached_dh = dh;
    }

    for (unsigned py = 0; py < dh; py++) {
        uint32_t sy = s_y_map[py];
        uint32_t *dst_row = s->pixels + (dy + py) * s->stride + dx;

        /* Dikey satır yinelemelerini (upscaling) doğrudan ultra-hızlı bellek kopyalaması ile geç */
        if (py > 0 && sy == s_y_map[py - 1]) {
            kmemcpy(dst_row, dst_row - s->stride, dw * sizeof(uint32_t));
            continue;
        }

        const uint32_t *src_row = src + sy * sw;
        unsigned px = 0;
        for (; px + 8 <= dw; px += 8) {
            dst_row[px + 0] = src_row[s_x_map[px + 0]];
            dst_row[px + 1] = src_row[s_x_map[px + 1]];
            dst_row[px + 2] = src_row[s_x_map[px + 2]];
            dst_row[px + 3] = src_row[s_x_map[px + 3]];
            dst_row[px + 4] = src_row[s_x_map[px + 4]];
            dst_row[px + 5] = src_row[s_x_map[px + 5]];
            dst_row[px + 6] = src_row[s_x_map[px + 6]];
            dst_row[px + 7] = src_row[s_x_map[px + 7]];
        }
        for (; px < dw; px++) {
            dst_row[px] = src_row[s_x_map[px]];
        }
    }
}

static void format_mmss(char *buf, double sec) {
    if (sec < 0) sec = 0;
    int s = (int)sec;
    int m = s / 60;
    s = s % 60;
    if (m > 99) m = 99;
    buf[0] = '0' + (m / 10);
    buf[1] = '0' + (m % 10);
    buf[2] = ':';
    buf[3] = '0' + (s / 10);
    buf[4] = '0' + (s % 10);
    buf[5] = 0;
}

/* 
 * Vektörel İkon Çizim Rutinleri (Geometric Vector Icon Pack):
 * UTF-8/emoji desteğine ihtiyaç duymadan daima keskin ve modern ikonlar.
 * '???' görünümünü tamamen ortadan kaldırır.
 */
static void draw_icon_play(gpu_surface_t *s, int cx, int cy, int sz, uint32_t color) {
    int half = sz / 2;
    for (int dy = -half; dy <= half; dy++) {
        int d = dy < 0 ? -dy : dy;
        int len = sz - (d * sz / (half > 0 ? half : 1));
        if (len > 0) {
            gpu_fill_rect(s, cx - half / 2, cy + dy, len, 1, color);
        }
    }
}

static void draw_icon_pause(gpu_surface_t *s, int cx, int cy, int h, uint32_t color) {
    int w = 3;
    int gap = 3;
    int top = cy - h / 2;
    gpu_fill_rect(s, cx - w - gap / 2, top, w, h, color);
    gpu_fill_rect(s, cx + gap / 2 + 1, top, w, h, color);
}

static void draw_icon_stop(gpu_surface_t *s, int cx, int cy, int sz, uint32_t color) {
    gpu_fill_rect(s, cx - sz / 2, cy - sz / 2, sz, sz, color);
}

static void draw_icon_prev(gpu_surface_t *s, int cx, int cy, uint32_t color) {
    gpu_fill_rect(s, cx - 6, cy - 5, 2, 11, color);
    for (int y = -5; y <= 5; y++) {
        int d = y < 0 ? -y : y;
        int w = 6 - d;
        if (w > 0) gpu_fill_rect(s, cx + 1 - w, cy + y, w, 1, color);
    }
}

static void draw_icon_next(gpu_surface_t *s, int cx, int cy, uint32_t color) {
    for (int y = -5; y <= 5; y++) {
        int d = y < 0 ? -y : y;
        int w = 6 - d;
        if (w > 0) gpu_fill_rect(s, cx - 1, cy + y, w, 1, color);
    }
    gpu_fill_rect(s, cx + 5, cy - 5, 2, 11, color);
}

static void draw_icon_loop(gpu_surface_t *s, int cx, int cy, uint32_t color) {
    /* Üst ok yönü (sağa) */
    gpu_fill_rect(s, cx - 6, cy - 4, 11, 2, color);
    gpu_fill_rect(s, cx - 6, cy - 4, 2, 5, color);
    gpu_fill_rect(s, cx + 3, cy - 6, 2, 2, color);
    gpu_fill_rect(s, cx + 5, cy - 4, 2, 2, color);
    /* Alt ok yönü (sola) */
    gpu_fill_rect(s, cx - 5, cy + 3, 11, 2, color);
    gpu_fill_rect(s, cx + 4, cy, 2, 5, color);
    gpu_fill_rect(s, cx - 5, cy + 5, 2, 2, color);
    gpu_fill_rect(s, cx - 7, cy + 3, 2, 2, color);
}

static void draw_icon_speaker(gpu_surface_t *s, int cx, int cy, int muted_flag, uint32_t color) {
    /* Hoparlör gövdesi (dikdörtgen) */
    gpu_fill_rect(s, cx - 6, cy - 2, 3, 5, color);
    /* Genişleyen koni */
    for (int y = -4; y <= 4; y++) {
        int d = y < 0 ? -y : y;
        int x0 = cx - 3 + d;
        int x1 = cx;
        if (x1 >= x0) gpu_fill_rect(s, x0, cy + y, x1 - x0 + 1, 1, color);
    }
    if (muted_flag) {
        /* Kırmızı sessiz (X) işareti */
        uint32_t red = 0xffef4444u;
        for (int d = -3; d <= 3; d++) {
            gpu_fill_rect(s, cx + 3 + d, cy + d, 2, 1, red);
            gpu_fill_rect(s, cx + 3 + d, cy - d, 2, 1, red);
        }
    } else {
        /* Ses dalgaları */
        gpu_fill_rect(s, cx + 3, cy - 2, 1, 5, color);
        gpu_fill_rect(s, cx + 4, cy - 3, 1, 1, color);
        gpu_fill_rect(s, cx + 4, cy + 3, 1, 1, color);
        gpu_fill_rect(s, cx + 6, cy - 4, 1, 9, color);
        gpu_fill_rect(s, cx + 7, cy - 5, 1, 1, color);
        gpu_fill_rect(s, cx + 7, cy + 5, 1, 1, color);
    }
}

static void draw_icon_fullscreen(gpu_surface_t *s, int cx, int cy, uint32_t color) {
    int sz = 5;
    /* Sol-üst */
    gpu_fill_rect(s, cx - sz, cy - sz, 4, 1, color);
    gpu_fill_rect(s, cx - sz, cy - sz, 1, 4, color);
    /* Sağ-üst */
    gpu_fill_rect(s, cx + sz - 3, cy - sz, 4, 1, color);
    gpu_fill_rect(s, cx + sz, cy - sz, 1, 4, color);
    /* Sol-alt */
    gpu_fill_rect(s, cx - sz, cy + sz, 4, 1, color);
    gpu_fill_rect(s, cx - sz, cy + sz - 3, 1, 4, color);
    /* Sağ-alt */
    gpu_fill_rect(s, cx + sz - 3, cy + sz, 4, 1, color);
    gpu_fill_rect(s, cx + sz, cy + sz - 3, 1, 4, color);
}

const char *media_title(void) {
    return title;
}

/* 
 * Sinematik Tek Kenarlık ve Akıcı Medya UI (VLC / Mediaplayer İbareleri Yoktur):
 * - Pencere başlık çubuğu tek ve tektir; pencere içinde mükerrer başlık kaldırılmıştır.
 * - Tek, net 1px sinematik kenarlık
 * - Neon cyan / saf beyaz modern kontrol arabirimi
 */
static int media_hit(int x,int y,int w,int h) {
    int side=sidebar_width(w),rw=w-side,rx=x-side;
    if(x<0||y<0||x>=w||y>=h) return -1;
    if(x<side) return y>=64&&y<64+playlist_count*62?100+(y-64)/62:-1;
    if(y>=h-104&&y<h-80&&rx>=24&&rx<rw-24) return 10;
    if(y>=h-56&&y<h-16) {
        if(rx>=16&&rx<48) return 2;
        if(rx>=58&&rx<98) return 0;
        if(rx>=108&&rx<140) return 3;
        if(rx>=152&&rx<184) return 1;
        if(rx>=196&&rx<228) return 4;
        if(rx>=rw-178&&rx<rw-146) return 5;
        if(rx>=rw-136&&rx<=rw-64) return 11;
        if(rx>=rw-48&&rx<rw-16) return 6;
    }
    if(y>=16&&y<h-120&&rx>=16&&rx<rw-16) return 20;
    return -1;
}
static void media_seek(double target) {
    if(!decoder) return;
    if(target<0) target=0;
    if(duration_seconds>0&&target>duration_seconds) target=duration_seconds;
    if(__builtin_setjmp(recovery)) {
        decoder=NULL; playing=0; audio_stop(); message="Unable to decode video."; changed=1; return;
    }
    audio_stop();
    if(playing) audio_start(plm_get_samplerate(decoder));
    plm_seek(decoder,target,1);
    last_tick=get_uptime_ms(); changed=1;
}
static void media_toggle(void) {
    if(!decoder) { media_refresh_playlist(); media_select_track(active_track_idx); return; }
    playing=!playing;
    if(playing) {
        audio_start(plm_get_samplerate(decoder));
        media_seek(plm_has_ended(decoder)?0:plm_get_time(decoder));
    } else audio_stop();
    last_tick=get_uptime_ms();
    message=playing?"Playing":"Paused"; changed=1;
}
void media_draw(gpu_surface_t *s,int x,int y,int w,int h) {
    if(w<440||h<220) return;
    uint64_t now=get_uptime_ms();
    if(!playlist_refreshed||now-playlist_refreshed>=2000) {
        media_refresh_playlist(); playlist_refreshed=now?now:1;
    }
    int side=sidebar_width(w),vx=x+side,vw=w-side;
    gpu_fill_rect(s,x,y,w,h,UI_PANEL);
    if(side) {
        gpu_fill_rect(s,x,y,side,h,UI_RAISED);
        gpu_fill_rect(s,x+side-1,y,1,h,UI_LINE);
        ui_text(s,x+22,y+22,"Library",UI_TEXT,1);
        for(int i=0;i<playlist_count;i++) {
            int yy=y+64+i*62;
            if(yy+58>y+h-66) break;
            if(i==active_track_idx||media_hover==100+i)
                ui_round(s,x+10,yy,side-20,56,10,i==active_track_idx?UI_HOVER:UI_RAISED);
            ui_round(s,x+22,yy+12,30,30,8,i==active_track_idx?UI_ACCENT:0xff475569u);
            draw_icon_play(s,x+35,yy+27,10,0xffffffff);
            ui_text_fit(s,x+64,yy+12,side-78,playlist[i].name,UI_TEXT,0);
            ui_text_fit(s,x+64,yy+34,side-78,playlist[i].desc,UI_MUTED,0);
        }
        ui_text(s,x+22,y+h-48,"Local Videos",UI_MUTED,0);
        ui_text(s,x+22,y+h-26,"/movies  ·  MPEG-1",UI_MUTED,0);
    }
    int bx=vx+16,by=y+16,bw=vw-32,bh=h-136;
    ui_round(s,bx,by,bw,bh,12,0xff111827);
    if(frames&&width&&height) {
        unsigned dw=width,dh=height;
        int aw=bw-16,ah=bh-16;
        if((uint64_t)width*ah>(uint64_t)aw*height) { dw=aw; dh=(uint64_t)height*aw/width; }
        else { dh=ah; dw=(uint64_t)width*ah/height; }
        video_blit_scaled(s,bx+(bw-dw)/2,by+(bh-dh)/2,dw,dh,pixels,width,height);
        if(!playing) {
            ui_round(s,bx+bw/2-24,by+bh/2-24,48,48,24,0xcfffffff);
            draw_icon_play(s,bx+bw/2-2,by+bh/2,16,0xff243249);
        }
    } else {
        ui_text_fit(s,bx+24,by+bh/2-24,bw-48,"Select a video",0xfff1f5fc,1);
        ui_text_fit(s,bx+24,by+bh/2+12,bw-48,message,0xffa4b1c4,0);
    }
    double cur=seek_preview>=0?seek_preview:(decoder?plm_get_time(decoder):0);
    int tx=vx+24,ty=y+h-96,tw=vw-48;
    ui_round(s,tx,ty,tw,4,2,UI_LINE);
    int progress=duration_seconds>0?(int)(cur*tw/duration_seconds):0;
    if(progress<0) progress=0; if(progress>tw) progress=tw;
    ui_round(s,tx,ty,progress,4,2,UI_ACCENT);
    ui_round(s,tx+progress-5,ty-3,10,10,5,UI_ACCENT);
    char time[16]; format_mmss(time,cur); ui_text(s,tx,y+h-80,time,UI_MUTED,0);
    format_mmss(time,duration_seconds); ui_text(s,vx+vw-66,y+h-80,time,UI_MUTED,0);
    int cy=y+h-36;
    const int centers[]={78,168,32,124,212,vw-162,vw-32};
    for(int id=0;id<7;id++) {
        int cx=vx+centers[id];
        uint32_t bg=id==0?UI_ACCENT:media_hover==id?UI_HOVER:UI_RAISED;
        if(id==4&&loop_enabled) bg=UI_HOVER;
        if(id==5&&muted) bg=UI_HOVER;
        ui_round(s,cx-18,cy-18,36,36,12,bg);
        uint32_t c=id==0?0xffffffff:UI_TEXT;
        if(id==0) { if(playing) draw_icon_pause(s,cx,cy,14,c); else draw_icon_play(s,cx-2,cy,14,c); }
        if(id==1) draw_icon_stop(s,cx,cy,10,c);
        if(id==2) draw_icon_prev(s,cx,cy,c);
        if(id==3) draw_icon_next(s,cx,cy,c);
        if(id==4) draw_icon_loop(s,cx,cy,loop_enabled?UI_ACCENT:c);
        if(id==5) draw_icon_speaker(s,cx,cy,muted,c);
        if(id==6) draw_icon_fullscreen(s,cx,cy,c);
    }
    int volx=vx+vw-136;
    ui_round(s,volx,cy-2,72,4,2,UI_LINE);
    ui_round(s,volx,cy-2,volume_pct*72/100,4,2,muted?UI_MUTED:UI_ACCENT);
    ui_round(s,volx+volume_pct*72/100-4,cy-4,8,8,4,muted?UI_MUTED:UI_ACCENT);
}
void media_click(int x,int y,int w,int h) {
    int hit=media_hit(x,y,w,h),rw=w-sidebar_width(w),rx=x-sidebar_width(w);
    if(hit>=100) { media_select_track(hit-100); changed=1; return; }
    if(hit==0||hit==20) media_toggle();
    if(hit==1&&decoder) { playing=0; media_seek(0); message="Durduruldu"; }
    if((hit==2||hit==3)&&decoder) media_seek(plm_get_time(decoder)+(hit==2?-10:10));
    if(hit==4) { loop_enabled=!loop_enabled; if(decoder) plm_set_loop(decoder,loop_enabled); }
    if(hit==5) { muted=!muted; audio_volume(muted?0:volume_pct); }
    if(hit==6) ui_toggle_maximize(5);
    if(hit==10&&decoder) media_seek((double)(rx-24)*duration_seconds/(rw-48));
    if(hit==11) {
        int value=(rx-(rw-136))*100/72; if(value<0) value=0; if(value>100) value=100;
        volume_pct=value; muted=0; audio_volume(volume_pct);
    }
    changed=1;
}
int media_pointer(int x,int y,int down,int w,int h) {
    int hit=media_hit(x,y,w,h),result=hit!=media_hover;
    media_hover=hit;
    if(down && (hit==10||hit==11) && !media_drag) media_drag=hit;
    if(media_drag==10) {
        int rw=w-sidebar_width(w),rx=x-sidebar_width(w);
        double value=(double)(rx-24)/(rw-48); if(value<0) value=0; if(value>1) value=1;
        seek_preview=value*duration_seconds; result=1;
        if(!down) { media_seek(seek_preview); seek_preview=-1; media_drag=0; }
    } else if(media_drag==11) {
        int rw=w-sidebar_width(w),rx=x-sidebar_width(w);
        int value=(rx-(rw-136))*100/72; if(value<0)value=0; if(value>100)value=100;
        volume_pct=value; muted=0; audio_volume(volume_pct); result=1;
        if(!down) media_drag=0;
    }
    return result;
}
void media_key(char key) {
    if(key==' ') media_toggle();
    else if(key=='m'||key=='M') { muted=!muted; audio_volume(muted?0:volume_pct); changed=1; }
    else if(key=='f'||key=='F') ui_toggle_maximize(5);
    else if(decoder&&(key==KBD_LEFT||key==KBD_RIGHT)) media_seek(plm_get_time(decoder)+(key==KBD_LEFT?-10:10));
}
const char *media_status(void) { return message; }
unsigned media_video_frames(void) { return frames; }
