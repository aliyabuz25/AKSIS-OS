#ifndef SHENLONG_ULTRALIGHT_BRIDGE_H
#define SHENLONG_ULTRALIGHT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct shenlong_ul_view shenlong_ul_view;

shenlong_ul_view *shenlong_ul_create(unsigned width, unsigned height);
int shenlong_ul_load_html(shenlong_ul_view *, const char *html, const char *base_url);
void shenlong_ul_update(shenlong_ul_view *);
const unsigned char *shenlong_ul_bitmap(shenlong_ul_view *, unsigned *width,
                                        unsigned *height, unsigned *row_bytes);
void shenlong_ul_destroy(shenlong_ul_view *);

#ifdef __cplusplus
}
#endif
#endif
