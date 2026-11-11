/*
 * AXIS OS - Native AI Assistant & System Copilot
 * OpenRouter Entegrasyonu & Akıcı Sağ Offcanvas Drawer
 */
#ifndef AXIS_AI_H
#define AXIS_AI_H

#include "kernel.h"
#include "gpu.h"

#define AI_DRAWER_WIDTH 520
#define AI_MAX_MSGS     16
#define AI_INPUT_MAX    160

typedef struct {
    char sender[16];
    char text[512];
    int  is_user;
} ai_msg_t;

extern int            ai_open;
extern int            ai_anim;
extern int            ai_closing;
extern int            ai_x;
extern uint64_t       ai_started;
extern gpu_surface_t *ai_surface;
extern int            ai_input_focus;
extern char           ai_input[AI_INPUT_MAX];
extern int            ai_input_len;

void ai_init(void);
void ai_toggle(void);
void ai_open_drawer(void);
void ai_close_drawer(void);
void ai_draw(gpu_surface_t *s, uint64_t now);
int  ai_hit_test(int x, int y);
void ai_click(int hit, int x, int y);
void ai_key(char c);
void ai_poll(uint64_t now);
void ai_send_query(const char *query);
void ai_draw_circle_icon(gpu_surface_t *s, int cx, int cy, int radius, int hot);

#endif /* AXIS_AI_H */
