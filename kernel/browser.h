#ifndef SHENLONG_BROWSER_H
#define SHENLONG_BROWSER_H
#include "gpu.h"
void browser_init(void);
void browser_draw(gpu_surface_t *s,int x,int y,int w,int h);
void browser_draw_local(gpu_surface_t *s,int x,int y,int w,int h);
int browser_key(char key);
void browser_click(int x,int y);
void browser_click_local(int x,int y);
int browser_poll(void);
void browser_navigate(const char *url);
const char *browser_status(void);
const char *browser_title(void);
const char *browser_current_url(void);
const char *browser_text_line(unsigned line);
#endif
