#ifndef SHENLONG_MEDIA_H
#define SHENLONG_MEDIA_H
#include "gpu.h"
int media_load(const void *data,unsigned size,const char *name);
void media_demo(void);
void media_bieber(void);
void media_stop(void);
int media_poll(void);
void media_draw(gpu_surface_t *s,int x,int y,int w,int h);
void media_click(int x,int y,int w,int h);
int media_pointer(int x,int y,int down,int w,int h);
void media_key(char key);
const char *media_title(void);
const char *media_status(void);
unsigned media_video_frames(void);
#endif
