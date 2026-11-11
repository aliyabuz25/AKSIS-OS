#ifndef SHENLONG_HTML_UI_H
#define SHENLONG_HTML_UI_H

#include "libc/stdint.h"
#include "libc/stddef.h"
#include "kernel.h"
#include "gpu.h"

#define HTML_UI_MAX_NODES 512

typedef enum {
    HTML_NODE_BLOCK,       /* div, section, card, header, main, footer */
    HTML_NODE_FLEX_ROW,    /* div with display:flex, class="row", class="flex-row" */
    HTML_NODE_HEADING,     /* h1, h2, h3, h4 */
    HTML_NODE_TEXT,        /* p, span, small */
    HTML_NODE_BADGE,       /* badge / pill */
    HTML_NODE_BUTTON,      /* button, a.btn */
    HTML_NODE_LINK,        /* a */
    HTML_NODE_METER,       /* progress / meter */
    HTML_NODE_HR,          /* horizontal rule */
    HTML_NODE_STAT_BOX,    /* stat / metric widget */
    HTML_NODE_TABLE,       /* table */
    HTML_NODE_TR,          /* table row */
    HTML_NODE_TH,          /* table header cell */
    HTML_NODE_TD,          /* table data cell */
    HTML_NODE_LIST_ITEM    /* li */
} html_node_type_t;

typedef struct {
    uint32_t bg_color;         /* Solid background color */
    uint32_t bg_color_bot;     /* Optional secondary color */
    uint32_t border_color;     /* Border stroke color */
    uint32_t text_color;       /* Text foreground color */
    uint8_t  border_radius;    /* Corner rounding */
    uint8_t  has_gradient;     /* 0=none, 1=vertical */
    uint8_t  font_size;        /* 0=regular (14px), 1=medium (16px), 2=large (20px), 3=hero (24px) */
    uint8_t  font_weight;      /* 0=normal, 1=bold */
    uint8_t  text_align;       /* 0=left, 1=center, 2=right */
    uint8_t  padding_x;        /* Horizontal padding */
    uint8_t  padding_y;        /* Vertical padding */
    uint8_t  margin_y;         /* Vertical margin */
    uint8_t  gap;              /* Gap between flex children */
    uint8_t  is_pill;          /* Pill badge shape */
    uint8_t  col_span;         /* Grid columns (1-12, 0=auto) */
    uint8_t  is_striped;       /* Zebra striping for table row */
} html_style_t;

typedef struct {
    html_node_type_t type;
    char tag[16];
    char class_name[48];
    char id[32];
    char text[512];
    char href[256];
    char subtext[64];
    html_style_t style;
    int16_t x, y, w, h;       /* Computed layout bounds */
    int16_t parent;
    int16_t first_child;
    int16_t next_sibling;
    uint8_t meter_val;        /* 0-100 for meter/progress */
    int handle, preferred_width, min_height;
} html_ui_node_t;

typedef struct {
    html_ui_node_t nodes[HTML_UI_MAX_NODES];
    int count;
    int content_height;
} html_ui_doc_t;

#include "axis_web/runtime.h"
void html_ui_import(const aw_node *nodes, int count, html_ui_doc_t *doc);

/* HTML5 Ultra Engine: Parser and Layout */
void html_ui_init(void);
int  html_ui_parse(const char *html, html_ui_doc_t *doc);
void html_ui_layout(html_ui_doc_t *doc, int max_w, int max_h);
void html_ui_render(gpu_surface_t *surf, int offset_x, int offset_y, int scroll_y, const html_ui_doc_t *doc);
int  html_ui_click(const html_ui_doc_t *doc, int click_x, int click_y, int scroll_y, char *out_action, unsigned cap);

/* AAA Graphical Primitives for HTML5/CSS3 rendering */
void html_render_gradient_v(gpu_surface_t *s, int x, int y, int w, int h, uint32_t top, uint32_t bot);
void html_render_gradient_h(gpu_surface_t *s, int x, int y, int w, int h, uint32_t left, uint32_t right);
void html_render_card(gpu_surface_t *s, int x, int y, int w, int h, int r, uint32_t bg_top, uint32_t bg_bot, uint32_t border_col);
void html_render_pill(gpu_surface_t *s, int x, int y, int w, int h, uint32_t bg_col, uint32_t border_col);
void html_render_meter(gpu_surface_t *s, int x, int y, int w, int h, int percent, uint32_t bar_col);

/* Built-in Bootstrap 5.3.3 & HTML5 Ultra Dashboards */
const char *html_ui_get_welcome_page(void);
const char *html_ui_get_system_page(void);
const char *html_ui_get_network_page(void);
const char *html_ui_get_about_page(void);
const char *html_ui_get_files_page(void);
const char *html_ui_get_bluetooth_page(void);
const char *html_ui_get_bootstrap_css(void);
void html_ui_build_system_page(char *out_buf, unsigned max_size);
void html_ui_get_page_content(const char *page_path, char *out_buf, unsigned max_size);
void html_ui_interpolate(const char *tmpl, char *out, unsigned max_size);

#endif
