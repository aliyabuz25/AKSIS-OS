#ifndef UI_FONT_H
#define UI_FONT_H
#include "gpu.h"
void ui_text(gpu_surface_t *s, int x, int y, const char *str, uint32_t color, int size);
int ui_text_measure(const char *str, int size);
#endif
