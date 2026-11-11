#ifndef AXIS_WEB_H
#define AXIS_WEB_H
#include "gpu.h"
void axis_web_init(void);
int axis_web_open(int app,const char *path);
int axis_web_load(int app,const char *url,const char *html,unsigned size,int scripts);
void axis_web_close(int app);
void axis_web_draw(int app,gpu_surface_t *s,int x,int y,int w,int h);
int axis_web_click(int app,int x,int y,char *action,unsigned cap);
int axis_web_key(int app,char key);
unsigned axis_web_poll(void);
int axis_web_eval(int app,const char *script,char *out,unsigned cap);
const char *axis_web_status(int app);
#endif
