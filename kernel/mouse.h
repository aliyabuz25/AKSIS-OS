/*
 * SHENLONG OS - PS/2 Mouse + Cursor
 */
#ifndef MOUSE_H
#define MOUSE_H

#include "kernel.h"

typedef struct {
    int32_t  x, y;          /* piksel pozisyon */
    int32_t  dx, dy;        /* son delta */
    uint8_t  buttons;       /* bit0=sol, bit1=sag, bit2=orta */
    uint8_t  pressed;       /* latched until the UI consumes the press */
    int32_t  press_x, press_y;
    int      moved;         /* bu frame'de hareket etti mi */
} mouse_state_t;

extern mouse_state_t g_mouse;

void mouse_init(void);
int mouse_available(void);
void mouse_set_visible(int visible);
void mouse_handle_byte(uint8_t data);
void mouse_poll(void);      /* ana döngüde çağır */

/* Cursor çiz/sil */
void cursor_draw(void);
void cursor_erase(void);
void cursor_invalidate(void);
void cursor_update(void);   /* poll + redraw */

#endif
