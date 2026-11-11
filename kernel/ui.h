#ifndef UI_H
#define UI_H
#include "gpu.h"
void ui_init(void);
void ui_desktop_draw(void);
void ui_flush(void);
void ui_poll(void);
int ui_handle_key(char key);
int ui_terminal_focused(void);
const char *ui_take_command(void);
void ui_open_browser(const char *url);
void ui_open_media(void);
void ui_toggle_maximize(int app);
void ui_on_display_resize(uint32_t new_w, uint32_t new_h);
#endif
