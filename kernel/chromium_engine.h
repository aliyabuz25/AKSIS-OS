#ifndef SHENLONG_CHROMIUM_ENGINE_H
#define SHENLONG_CHROMIUM_ENGINE_H

#include "libc/stdint.h"
#include "libc/stddef.h"
#include "kernel.h"
#include "gpu.h"

#define CHROMIUM_MAX_NODES 256
#define CHROMIUM_CONSOLE_LINES 32

typedef enum {
    DOM_NODE_ELEMENT,
    DOM_NODE_TEXT,
    DOM_NODE_COMMENT
} dom_node_type_t;

typedef struct {
    dom_node_type_t type;
    char tag[16];
    char id[32];
    char class_name[48];
    char href[192];
    char src[192];
    char text[160];
    uint32_t color;
    uint32_t bg_color;
    uint8_t font_size;
    uint8_t is_bold;
    uint8_t is_block;
    uint8_t align; /* 0=left, 1=center, 2=right */
    int16_t x, y, w, h;
    int16_t parent;
    int16_t first_child;
    int16_t next_sibling;
} dom_node_t;

typedef struct {
    dom_node_t nodes[CHROMIUM_MAX_NODES];
    int node_count;
    int root;
} dom_tree_t;

typedef enum {
    DEVTOOLS_TAB_CONSOLE,
    DEVTOOLS_TAB_ELEMENTS,
    DEVTOOLS_TAB_NETWORK,
    DEVTOOLS_TAB_SOURCES
} devtools_tab_t;

typedef struct {
    int open;
    int height;
    devtools_tab_t active_tab;
    char console_log[CHROMIUM_CONSOLE_LINES][96];
    int console_count;
    char cmd_input[96];
    int cmd_len;
} devtools_state_t;

void chromium_engine_init(void);
void chromium_dom_build(const char *html, unsigned len, dom_tree_t *tree);
void chromium_dom_layout(dom_tree_t *tree, int view_w, int view_h, int *total_h);
void chromium_dom_render(gpu_surface_t *s, int x, int y, int w, int h, int scroll_y, dom_tree_t *tree);
int  chromium_dom_click(dom_tree_t *tree, int click_x, int click_y, int scroll_y, char *out_href);

/* V8-Lite JavaScript Evaluator */
int  chromium_js_eval(const char *script, char *out_result, unsigned out_cap);
void chromium_console_log(const char *msg);

/* DevTools Drawer */
void chromium_devtools_draw(gpu_surface_t *s, int x, int y, int w, int h);
void chromium_devtools_click(int x, int y, int w, int h);
int  chromium_devtools_key(char c);

extern devtools_state_t g_devtools;
extern dom_tree_t g_dom_tree;

#endif
