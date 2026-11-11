#ifndef AXIS_WEB_RUNTIME_H
#define AXIS_WEB_RUNTIME_H
/* ABI boundary: deliberately independent of kernel and hosted libc typedefs. */
typedef struct aw_view aw_view;
typedef struct {
    void *opaque;
    const char *(*read)(void *, const char *path, unsigned long *size);
    unsigned long long (*now)(void *);
    void (*log)(void *, const char *);
} aw_host;
typedef struct {
    int handle, parent;
    char tag[16], classes[96], id[64], text[512], href[256];
    unsigned color, background, border;
    unsigned flags;
    int display, font_size, weight, align, padding_x, padding_y, gap, margin;
    int width, min_height, radius;
} aw_node;
enum { AW_COLOR=1, AW_BG=2, AW_BORDER=4, AW_PADDING=8, AW_GAP=16, AW_MARGIN=32 };
aw_view *aw_create(const aw_host *host);
void aw_destroy(aw_view *v);
int aw_load(aw_view *v, const char *url, const char *html, unsigned long size, int scripts);
int aw_eval(aw_view *v, const char *js, char *result, unsigned cap);
int aw_poll(aw_view *v);
int aw_export(aw_view *v, aw_node *nodes, unsigned capacity);
int aw_click(aw_view *v, int handle); /* nonzero if preventDefault() was called */
const char *aw_error(const aw_view *v);
int aw_resolve(const char *base, const char *reference, char *out, unsigned cap);
void axis_memory_init(void *, unsigned long);
unsigned long axis_memory_live(void);
unsigned long axis_memory_peak(void);
#endif
