/*
 * SHENLONG OS - Framebuffer Text Renderer
 * GPU compositor backend uzerinde calisir
 * Subpixel AA, ANSI renk, native 8x16 font
 */

#include "fb.h"
#include "gpu.h"
#include "kernel.h"
#include "font8x16.h"
#include "terminal_extra.h"
#include "ps2.h"
#include "ui.h"

/* ------------------------------------------------------------------ */
/* Globals — GPU'dan mirror                                           */
/* ------------------------------------------------------------------ */
uint8_t  *fb_addr   = NULL;
uint32_t  fb_width  = 0;
uint32_t  fb_height = 0;
uint32_t  fb_pitch  = 0;
uint32_t  fb_bpx    = 0;

int fb_row = 0;
int fb_col = 0;

static uint32_t cols = 0;
static uint32_t rows = 0;

/* GPU terminal surface */
static gpu_surface_t *term_surf = NULL;
static int present_pending;
#define CELL_COLS 256
#define CELL_ROWS 128
typedef struct { char c; uint32_t fg, bg; } term_cell_t;
static term_cell_t cells[CELL_ROWS][CELL_COLS];
static void draw_char(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg);
static void mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* ------------------------------------------------------------------ */
/* Output capture                                                     */
/* ------------------------------------------------------------------ */
static char    *cap_buf = NULL;
static uint32_t cap_len = 0;
static uint32_t cap_max = 0;

void fb_capture_start(char *buf, uint32_t size) {
    cap_buf = buf; cap_len = 0; cap_max = size;
    if (cap_max > 0) cap_buf[0] = '\0';
}
void fb_capture_end(void) {
    if (cap_buf && cap_max > 0) cap_buf[cap_len] = '\0';
    cap_buf = NULL; cap_len = 0; cap_max = 0;
}
uint32_t fb_capture_len(void) { return cap_len; }

/* ------------------------------------------------------------------ */
/* Renk durumu                                                        */
/* ------------------------------------------------------------------ */
static uint32_t cur_fg = TERM_FG;
static uint32_t cur_bg = TERM_BG;

void fb_set_fg(uint32_t c) { cur_fg = c; }
void fb_set_bg(uint32_t c) { cur_bg = c; }

static const uint32_t ansi_pal[16] = {
    0x000000, 0xCC0000, 0x00BB00, 0xBBBB00,
    0x0055CC, 0xAA00AA, 0x00AAAA, 0xBBBBBB,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};

#define ANSI_MAX 8
static int ansi_state = 0;
static int ansi_params[ANSI_MAX];
static int ansi_np = 0;
static unsigned utf_code,utf_left;

static void ansi_reset(void) { cur_fg = TERM_FG; cur_bg = TERM_BG; }

static void ansi_apply(void) {
    for (int i = 0; i < ansi_np; i++) {
        int p = ansi_params[i];
        if (p == 0)                    ansi_reset();
        else if (p >= 30 && p <= 37)   cur_fg = ansi_pal[p-30];
        else if (p == 39)              cur_fg = TERM_FG;
        else if (p >= 40 && p <= 47)   cur_bg = ansi_pal[p-40];
        else if (p == 49)              cur_bg = TERM_BG;
        else if (p >= 90 && p <= 97)   cur_fg = ansi_pal[p-90+8];
        else if (p >= 100 && p <= 107) cur_bg = ansi_pal[p-100+8];
    }
}

/* ------------------------------------------------------------------ */
/* Cursor (block)                                                     */
/* ------------------------------------------------------------------ */
static int caret_enabled = 0;
static int caret_px = -1, caret_py = -1;
static uint32_t caret_saved[FONT_W * FONT_H];

static void caret_erase(void) {
    if (!term_surf || caret_px < 0) return;
    mark_dirty(caret_px,caret_py,FONT_W,FONT_H);
    for (int y = 0; y < (int)FONT_H; y++)
        for (int x = 0; x < (int)FONT_W; x++) {
            int px = caret_px + x, py = caret_py + y;
            if (px < (int)term_surf->w && py < (int)term_surf->h)
                term_surf->pixels[(uint32_t)py * term_surf->stride + (uint32_t)px] =
                    caret_saved[(uint32_t)y * FONT_W + (uint32_t)x];
        }
    caret_px = caret_py = -1;
}

static void caret_draw(void) {
    if (!caret_enabled || !term_surf) return;
    int col = fb_col, row = fb_row;
    if (col >= (int)cols) { col = 0; row++; }
    if (row >= (int)rows) return;
    int px = col * FONT_W, py = row * FONT_H;
    mark_dirty(px,py,FONT_W,FONT_H);
    caret_px = px; caret_py = py;
    for (int y = 0; y < (int)FONT_H; y++)
        for (int x = 0; x < (int)FONT_W; x++) {
            uint32_t *p = &term_surf->pixels[(uint32_t)(py+y)*term_surf->stride+(uint32_t)(px+x)];
            caret_saved[(uint32_t)y*FONT_W+(uint32_t)x] = *p;
            if (x < 2) *p = GPU_RGB(99, 224, 190);
        }
}

/* ------------------------------------------------------------------ */
/* Dirty rect + flush                                                 */
/* ------------------------------------------------------------------ */
static uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;
static unsigned update_depth = 0;

static void mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (x < dirty_x0) dirty_x0 = x;
    if (y < dirty_y0) dirty_y0 = y;
    if (x+w > dirty_x1) dirty_x1 = x+w;
    if (y+h > dirty_y1) dirty_y1 = y+h;
}

static void begin_update(void) {
    if (update_depth++ == 0) {
        dirty_x0 = fb_width; dirty_y0 = fb_height;
        dirty_x1 = dirty_y1 = 0;
        caret_erase();
    }
}

static void end_update(void) {
    if (--update_depth) return;
    ps2_poll();
    caret_draw();
    present_pending = 1;
    if(dirty_x1>dirty_x0 && dirty_y1>dirty_y0)
        gpu_surface_damage(term_surf,dirty_x0,dirty_y0,dirty_x1-dirty_x0,dirty_y1-dirty_y0);
    /* Sadece her 32 karakterde bir ui_poll — terminal yazarken kasmasın */
    static unsigned poll_tick = 0;
    if (++poll_tick >= 32) { poll_tick = 0; ui_poll(); }
}

void fb_present(void) {
    present_pending = 0;
    gpu_compose(); /* also retries a queued frame after the pacing deadline */
}

void fb_set_viewport(int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!term_surf || w < FONT_W || h < FONT_H || w > fb_width || h > fb_height) return;
    caret_erase();
    term_surf->x = x; term_surf->y = y;
    term_surf->w = w; term_surf->h = h; term_surf->stride = w;
    term_surf->z = 10;
    cols = w / FONT_W; rows = h / FONT_H;
    fb_clear(TERM_BG);
}

void fb_window(int x, int y, uint32_t w, uint32_t h, int z, int visible) {
    if (!term_surf || w < FONT_W || h < FONT_H || w > fb_width || h > fb_height) return;
    if(term_surf->x==x && term_surf->y==y && term_surf->w==w && term_surf->h==h &&
       term_surf->z==z && term_surf->visible==visible) return;
    caret_erase();
    if (w != term_surf->w || h != term_surf->h) {
        uint32_t new_rows=h/FONT_H;
        if (fb_row >= (int)new_rows) {
            unsigned shift=fb_row-new_rows+1;
            for (unsigned row=0;row<CELL_ROWS;row++) {
                if(row+shift<CELL_ROWS) kmemcpy(cells[row],cells[row+shift],sizeof(cells[row]));
                else kmemset(cells[row],0,sizeof(cells[row]));
            }
            fb_row-=shift;
        }
        term_surf->w=w; term_surf->h=h; term_surf->stride=w;
        cols=w/FONT_W; rows=new_rows;
        if(cols>CELL_COLS) cols=CELL_COLS;
        if(rows>CELL_ROWS) rows=CELL_ROWS;
        if(fb_col>(int)cols) fb_col=cols;
        gpu_fill(term_surf, TERM_BG);
        for(unsigned row=0;row<rows;row++) for(unsigned col=0;col<cols;col++) {
            term_cell_t c=cells[row][col];
            if(c.c) draw_char(col,row,c.c,c.fg,c.bg);
        }
    }
    term_surf->x=x; term_surf->y=y; term_surf->z=z; term_surf->visible=visible;
    caret_draw();
    present_pending=1;
}

void fb_window_alpha(int alpha) { gpu_surface_alpha(term_surf,alpha); }

/* ------------------------------------------------------------------ */
/* Karakter cizimi — subpixel AA, GPU surface uzerine                */
/* ------------------------------------------------------------------ */
static inline uint8_t blend_ch(uint8_t f, uint8_t b, uint8_t a) {
    return (uint8_t)((f*a + b*(255u-a)) / 255u);
}

static void draw_char(uint32_t col, uint32_t row, char c,
                      uint32_t fg, uint32_t bg) {
    if (!term_surf) return;
    unsigned code=(uint8_t)c;
    if (code < 0x20 || (code>0x7e&&(code<0x80||code>=0x80+sizeof(terminal_extra)/sizeof(terminal_extra[0])))) c = ' ';
    if (row<CELL_ROWS && col<CELL_COLS) cells[row][col]=(term_cell_t){c,fg,bg};
    const uint8_t *glyph = (uint8_t)c>=0x80?terminal_extra[(uint8_t)c-0x80].rows:vga_font[(uint8_t)c - 0x20];
    uint32_t bx = col * FONT_W;
    uint32_t by = row * FONT_H;
    mark_dirty(bx, by, FONT_W, FONT_H);

    uint8_t fg_r = GPU_R(fg), fg_g = GPU_G(fg), fg_b = GPU_B(fg);
    uint8_t bg_r = GPU_R(bg), bg_g = GPU_G(bg), bg_b = GPU_B(bg);

    for (uint32_t gy = 0; gy < (uint32_t)GLYPH_H; gy++) {
        uint8_t bits = glyph[gy];
        uint8_t prev = gy > 0            ? glyph[gy-1] : 0;
        uint8_t next = gy < (uint32_t)GLYPH_H-1 ? glyph[gy+1] : 0;

        for (uint32_t gx = 0; gx < (uint32_t)GLYPH_W; gx++) {
            uint32_t mask = 0x80u >> gx;
            int on = (bits & mask) != 0;
            uint32_t px = bx + gx, py = by + gy;
            if (px >= term_surf->w || py >= term_surf->h) continue;
            uint32_t *dp = &term_surf->pixels[py * term_surf->stride + px];

            if (on) {
                *dp = GPU_RGBA(fg_r, fg_g, fg_b, 0xFF);
            } else {
                uint32_t lm = gx > 0            ? (0x80u>>(gx-1)) : 0;
                uint32_t rm = gx < (uint32_t)GLYPH_W-1 ? (0x80u>>(gx+1)) : 0;
                int lo = (bits & lm) != 0;
                int ro = (bits & rm) != 0;
                int ao = (prev & mask) != 0;
                int bo = (next & mask) != 0;

                /* A bitmap font is already pixel-aligned. Keep it crisp. */
                (void)lo; (void)ro; (void)ao; (void)bo;
                uint8_t a_r = 0, a_g = 0, a_b = 0;

                if (a_r || a_g || a_b) {
                    *dp = GPU_RGBA(
                        blend_ch(fg_r, bg_r, a_r),
                        blend_ch(fg_g, bg_g, a_g),
                        blend_ch(fg_b, bg_b, a_b),
                        0xFF);
                } else {
                    *dp = GPU_RGBA(bg_r, bg_g, bg_b, 0xFF);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* fb_init                                                            */
/* ------------------------------------------------------------------ */
void fb_init(void *addr, uint32_t w, uint32_t h, uint32_t pitch, uint16_t bpp) {
    fb_addr  = (uint8_t *)addr;
    fb_width = w; fb_height = h; fb_pitch = pitch;
    fb_bpx   = bpp / 8;

    /* GPU compositor'u baslat */
    gpu_init(addr, w, h, pitch, bpp);

    cols = w / FONT_W;
    rows = h / FONT_H;
    fb_row = 0; fb_col = 0;
    cur_fg = TERM_FG; cur_bg = TERM_BG;

    /* Terminal surface: z=1, tam ekran */
    term_surf = gpu_surface_create(w, h, 0, 0, 1);
    if (term_surf) {
        /* Siyah ile doldur */
        uint32_t n = w * h;
        for (uint32_t i = 0; i < n; i++)
            term_surf->pixels[i] = GPU_RGBA(0,0,0,0xFF);
    }

    update_depth = 0;
}

/* ------------------------------------------------------------------ */
/* fb_clear                                                          */
/* ------------------------------------------------------------------ */
void fb_clear(uint32_t color) {
    begin_update();
    kmemset(cells,0,sizeof(cells));
    uint32_t fill = GPU_RGBA(GPU_R(color), GPU_G(color), GPU_B(color), 0xFF);
    if (term_surf) {
        uint32_t n = term_surf->w * term_surf->h;
        for (uint32_t i = 0; i < n; i++) term_surf->pixels[i] = fill;
    }
    fb_row = 0; fb_col = 0;
    ansi_state = 0; utf_left=0; cur_fg = TERM_FG; cur_bg = TERM_BG;
    mark_dirty(0, 0, fb_width, fb_height);
    end_update();
}

/* ------------------------------------------------------------------ */
/* fb_scroll                                                         */
/* ------------------------------------------------------------------ */
void fb_scroll(void) {
    if (!term_surf) return;
    begin_update();
    for (unsigned row=0;row+1<rows;row++)
        kmemcpy(cells[row],cells[row+1],sizeof(cells[row]));
    kmemset(cells[rows-1],0,sizeof(cells[rows-1]));
    uint32_t ch = FONT_H;
    uint32_t sw = term_surf->w;
    uint32_t sh = term_surf->h;
    uint32_t stride = term_surf->stride;

    /* Satirlari yukari kopyala (64-bit hizali) */
    for (uint32_t y = 0; y + ch < rows * FONT_H; y++) {
        uint32_t *dst = term_surf->pixels + y * stride;
        uint32_t *src = term_surf->pixels + (y + ch) * stride;
        uint32_t x = 0;
        for (; x + 2 <= sw; x += 2)
            *(uint64_t*)(dst+x) = *(uint64_t*)(src+x);
        if (x < sw) dst[x] = src[x];
    }
    /* Son satiri temizle */
    uint32_t y0 = (rows-1) * FONT_H;
    uint32_t bg_fill = GPU_RGBA(GPU_R(cur_bg), GPU_G(cur_bg), GPU_B(cur_bg), 0xFF);
    for (uint32_t y = y0; y < sh; y++) {
        uint32_t *row = term_surf->pixels + y * stride;
        for (uint32_t x = 0; x < sw; x++) row[x] = bg_fill;
    }
    fb_row = (int)(rows-1);
    mark_dirty(0, 0, fb_width, fb_height);
    end_update();
}

/* ------------------------------------------------------------------ */
/* fb_text_cursor                                                     */
/* ------------------------------------------------------------------ */
void fb_text_cursor(int enabled) {
    begin_update();
    caret_enabled = enabled;
    end_update();
}
void fb_text_move(int delta) {
    begin_update();
    int position=fb_row*(int)cols+fb_col+delta;
    if(position<0) position=0;
    if(position>=(int)(rows*cols)) position=rows*cols-1;
    fb_row=position/cols; fb_col=position%cols;
    end_update();
}

/* ------------------------------------------------------------------ */
/* fb_putc — ANSI escape + subpixel AA                               */
/* ------------------------------------------------------------------ */
void fb_putc(char c) {
    if (cap_buf) {
        if (cap_len + 1 < cap_max) { cap_buf[cap_len++] = c; cap_buf[cap_len] = '\0'; }
        return;
    }
    if (!fb_addr || !term_surf) return;
    unsigned byte=(uint8_t)c;
    if(byte>=128) {
        if(byte>=0xc2&&byte<=0xdf) { utf_code=byte&31; utf_left=1; return; }
        if(byte>=0xe0&&byte<=0xef) { utf_code=byte&15; utf_left=2; return; }
        if((byte&0xc0)!=0x80||!utf_left) return;
        utf_code=(utf_code<<6)|(byte&63);
        if(--utf_left) return;
        c='?';
        for(unsigned i=0;i<sizeof(terminal_extra)/sizeof(terminal_extra[0]);i++)
            if(terminal_extra[i].code==utf_code) { c=(char)(0x80+i); break; }
    } else utf_left=0;

    /* ANSI state machine */
    if (ansi_state == 1) {
        if (c == '[') { ansi_state = 2; ansi_np = 0; ansi_params[0] = 0; return; }
        ansi_state = 0;
    }
    if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            if (ansi_np == 0) ansi_np = 1;
            ansi_params[ansi_np-1] = ansi_params[ansi_np-1]*10 + (c-'0');
            return;
        }
        if (c == ';') {
            if (ansi_np < ANSI_MAX) ansi_params[ansi_np++] = 0;
            return;
        }
        if (c == 'm') { ansi_apply(); ansi_state = 0; return; }
        if (c == 'J') { fb_clear(TERM_BG); ansi_state = 0; return; }
        ansi_state = 0; return;
    }
    if (c == '\x1b') { ansi_state = 1; return; }

    begin_update();

    if (c == '\n') {
        fb_col = 0; fb_row++;
    } else if (c == '\r') {
        fb_col = 0;
    } else if (c == '\b') {
        if (fb_col == 0 && fb_row > 0) { fb_row--; fb_col = (int)cols; }
        if (fb_col > 0) {
            fb_col--;
            draw_char((uint32_t)fb_col, (uint32_t)fb_row, ' ', cur_fg, cur_bg);
        }
        end_update(); return;
    } else if (c == '\t') {
        int next = (fb_col + 8) & ~7;
        while (fb_col < next && fb_col < (int)cols) {
            draw_char((uint32_t)fb_col, (uint32_t)fb_row, ' ', cur_fg, cur_bg);
            fb_col++;
        }
    } else {
        if (fb_col >= (int)cols) { fb_col = 0; fb_row++; }
        if (fb_row >= (int)rows) fb_scroll();
        draw_char((uint32_t)fb_col, (uint32_t)fb_row, c, cur_fg, cur_bg);
        fb_col++;
        end_update(); return;
    }
    if (fb_row >= (int)rows) fb_scroll();
    end_update();
}

/* ------------------------------------------------------------------ */
/* fb_puts                                                           */
/* ------------------------------------------------------------------ */
void fb_puts(const char *s) {
    while (*s) {
        begin_update();
        for (unsigned i = 0; i < 64 && *s; i++) fb_putc(*s++);
        end_update();
    }
}

void fb_puts_color(const char *s, uint32_t fg) {
    uint32_t old = cur_fg; cur_fg = fg;
    fb_puts(s);
    cur_fg = old;
}

void fb_newline(void) { fb_putc('\n'); }

/* ------------------------------------------------------------------ */
/* GPU primitives — fb.h'dan expose edilenler                        */
/* ------------------------------------------------------------------ */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (term_surf) gpu_fill_rect(term_surf, (int32_t)x, (int32_t)y, w, h, color);
    gpu_surface_damage(term_surf,x,y,w,h);
    gpu_compose();
}
void fb_gradient_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t c0, uint32_t c1, int vertical) {
    if (!term_surf) return;
    if (vertical) gpu_gradient_v(term_surf,(int32_t)x,(int32_t)y,w,h,c0,c1);
    else          gpu_gradient_v(term_surf,(int32_t)x,(int32_t)y,w,h,c0,c1);
    gpu_surface_damage(term_surf,x,y,w,h);
    gpu_compose();
}
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (term_surf) gpu_draw_rect(term_surf,(int32_t)x,(int32_t)y,w,h,color,1);
    gpu_surface_damage(term_surf,x,y,w,h);
    gpu_compose();
}
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (term_surf) gpu_draw_line(term_surf,x0,y0,x1,y1,color);
    int left=x0<x1?x0:x1, top=y0<y1?y0:y1;
    int right=x0>x1?x0:x1, bottom=y0>y1?y0:y1;
    gpu_surface_damage(term_surf,left,top,right-left+1,bottom-top+1);
    gpu_compose();
}
