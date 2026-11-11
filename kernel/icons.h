#ifndef SHENLONG_ICONS_H
#define SHENLONG_ICONS_H
#include "gpu.h"
void ui_icon(gpu_surface_t *dst,int x,int y,int app,int size);
void ui_icon_scaled(gpu_surface_t *dst,int x,int y,int app,int size);
void ui_icon_nobg(gpu_surface_t *dst,int x,int y,int app,int size);
#endif
