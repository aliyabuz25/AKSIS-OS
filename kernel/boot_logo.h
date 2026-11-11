#ifndef BOOT_LOGO_H
#define BOOT_LOGO_H
#include "gpu.h"
/* Aspect-preserving, centered fit inside the supplied rectangle. */
void boot_logo_draw(gpu_surface_t *s,int x,int y,int w,int h);
#endif
