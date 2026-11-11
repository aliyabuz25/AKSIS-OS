#include "html_ui.h"
#include "ui_font.h"
#include "ui_paint.h"
#include "theme.h"
#include "kernel_info.h"
#include "ui_assets.h"

static int to_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int str_equal(const char *a, const char *b) {
    while(*a && *b) {
        if(to_lower((uint8_t)*a) != to_lower((uint8_t)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int str_contains(const char *haystack, const char *needle) {
    if(!haystack || !needle || !*needle) return 0;
    int nlen = 0;
    while(needle[nlen]) nlen++;
    for(int i = 0; haystack[i]; i++) {
        int match = 1;
        for(int j = 0; j < nlen; j++) {
            if(!haystack[i+j] || to_lower((uint8_t)haystack[i+j]) != to_lower((uint8_t)needle[j])) {
                match = 0; break;
            }
        }
        if(match) return 1;
    }
    return 0;
}

/* CSS classes are whitespace-separated tokens, not substrings. */
static int class_has(const char *list,const char *name) {
    int n=kstrlen(name);
    while(*list) {
        while(*list==' '||*list=='\t'||*list=='\n'||*list=='\r') list++;
        const char *start=list;
        while(*list&&*list!=' '&&*list!='\t'&&*list!='\n'&&*list!='\r') list++;
        if(list-start==n&&!kstrncmp(start,name,n)) return 1;
    }
    return 0;
}

static uint32_t parse_hex_color(const char *s, uint32_t def_val) {
    if(!s) return def_val;
    while(*s == ' ' || *s == '#') s++;
    uint32_t val = 0;
    int digits = 0;
    while((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F')) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' :
                (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : *s - 'A' + 10;
        val = (val << 4) | d;
        s++; digits++;
        if(digits == 6) break;
    }
    if(digits == 6) return val | 0xff000000u;
    return def_val;
}

static void parse_attribute(const char *tag_str, unsigned len, const char *attr_name, char *out, unsigned out_cap) {
    out[0] = 0;
    unsigned n = 0;
    while(attr_name[n]) n++;
    for(unsigned i = 0; i + n + 1 < len; i++) {
        if((i == 0 || tag_str[i-1] == ' ' || tag_str[i-1] == '\t') &&
           !kstrncmp(tag_str + i, attr_name, n) && tag_str[i+n] == '=') {
            unsigned p = i + n + 1;
            char quote = 0;
            if(p < len && (tag_str[p] == '\"' || tag_str[p] == '\'')) quote = tag_str[p++];
            unsigned w = 0;
            while(p < len && (quote ? tag_str[p] != quote : (tag_str[p] != ' ' && tag_str[p] != '>'))) {
                if(w + 1 < out_cap) out[w++] = tag_str[p];
                p++;
            }
            out[w] = 0;
            return;
        }
    }
}

/* Shared drawing primitives */

void html_render_gradient_v(gpu_surface_t *s, int x, int y, int w, int h, uint32_t top, uint32_t bot) {
    if(!s || w <= 0 || h <= 0) return;
    int r1 = GPU_R(top), g1 = GPU_G(top), b1 = GPU_B(top), a1 = GPU_A(top);
    int r2 = GPU_R(bot), g2 = GPU_G(bot), b2 = GPU_B(bot), a2 = GPU_A(bot);

    for(int dy = 0; dy < h; dy++) {
        int t = (h > 1) ? (dy * 256 / (h - 1)) : 0;
        int r = r1 + ((r2 - r1) * t) / 256;
        int g = g1 + ((g2 - g1) * t) / 256;
        int b = b1 + ((b2 - b1) * t) / 256;
        int a = a1 + ((a2 - a1) * t) / 256;
        uint32_t col = (a << 24) | (r << 16) | (g << 8) | b;
        gpu_fill_rect(s, x, y + dy, w, 1, col);
    }
}

void html_render_gradient_h(gpu_surface_t *s, int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if(!s || w <= 0 || h <= 0) return;
    int r1 = GPU_R(left), g1 = GPU_G(left), b1 = GPU_B(left), a1 = GPU_A(left);
    int r2 = GPU_R(right), g2 = GPU_G(right), b2 = GPU_B(right), a2 = GPU_A(right);

    for(int dx = 0; dx < w; dx++) {
        int t = (w > 1) ? (dx * 256 / (w - 1)) : 0;
        int r = r1 + ((r2 - r1) * t) / 256;
        int g = g1 + ((g2 - g1) * t) / 256;
        int b = b1 + ((b2 - b1) * t) / 256;
        int a = a1 + ((a2 - a1) * t) / 256;
        uint32_t col = (a << 24) | (r << 16) | (g << 8) | b;
        gpu_fill_rect(s, x + dx, y, 1, h, col);
    }
}

void html_render_card(gpu_surface_t *s, int x, int y, int w, int h, int r, uint32_t bg_top, uint32_t bg_bot, uint32_t border_col) {
    if(!s || w <= 0 || h <= 0) return;
    if(r < 0) r = 0;
    if(border_col) {
        ui_round(s, x, y, w, h, r, border_col);
        if(w > 2 && h > 2) {
            int ir = (r > 1) ? r - 1 : 0;
            ui_round(s, x + 1, y + 1, w - 2, h - 2, ir, bg_top);
            if(bg_top != bg_bot && h > 4) {
                html_render_gradient_v(s, x + (r > 2 ? 2 : 1), y + 1, w - (r > 2 ? 4 : 2), h - 2, bg_top, bg_bot);
            }
        }
    } else {
        ui_round(s, x, y, w, h, r, bg_top);
        if(bg_top != bg_bot && h > 4) {
            html_render_gradient_v(s, x + (r > 2 ? 2 : 1), y + 1, w - (r > 2 ? 4 : 2), h - 2, bg_top, bg_bot);
        }
    }
}

void html_render_pill(gpu_surface_t *s, int x, int y, int w, int h, uint32_t bg_col, uint32_t border_col) {
    int r = h / 2;
    if(r < 1) r = 1;
    if(border_col) {
        ui_round(s, x, y, w, h, r, border_col);
        ui_round(s, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 1, bg_col);
    } else {
        ui_round(s, x, y, w, h, r, bg_col);
    }
}

void html_render_meter(gpu_surface_t *s, int x, int y, int w, int h, int percent, uint32_t bar_col) {
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    int r = h / 2;
    if(r < 2) r = 2;
    /* Track: dark background with subtle border */
    ui_round(s, x, y, w, h, r, 0xff161b24u);
    gpu_draw_rect(s, x, y, w, h, 0x2affffffu, 1);
    /* Filled indicator */
    int fill_w = (w - 4) * percent / 100;
    if(fill_w >= 4) {
        ui_round(s, x + 2, y + 2, fill_w, h - 4, r > 2 ? r - 1 : 1, bar_col ? bar_col : 0xff38bdf8u);
    }
}

/* --- Supported Jade class vocabulary --- */

static void apply_css_classes(html_ui_node_t *node) {
    const char *cls = node->class_name;
    html_style_t *st = &node->style;

    /* Defaults */
    st->text_color = UI_TEXT;
    st->border_radius = 0;
    st->padding_x = 0;
    st->padding_y = 0;
    st->margin_y = 5;

    /* Bootstrap 5 Grid / Flexbox */
    if(class_has(cls, "row") || class_has(cls, "flex-row")) {
        node->type = HTML_NODE_FLEX_ROW;
        st->gap = 10;
        st->margin_y = 6;
    } else if(class_has(cls, "col-12")) {
        node->type = HTML_NODE_BLOCK;
        st->col_span = 12;
    } else if(class_has(cls, "col-6") || class_has(cls, "col-md-6")) {
        node->type = HTML_NODE_BLOCK;
        st->col_span = 6;
    } else if(class_has(cls, "col-4") || class_has(cls, "col-lg-4")) {
        node->type = HTML_NODE_BLOCK;
        st->col_span = 4;
    } else if(class_has(cls, "col-3") || class_has(cls, "col-md-3")) {
        node->type = HTML_NODE_BLOCK;
        st->col_span = 3;
    } else if(class_has(cls, "col")) {
        node->type = HTML_NODE_BLOCK;
    }

    /* Fluent settings primitives used by the system pages. */
    if(class_has(cls, "settings-layout")) { node->type = HTML_NODE_FLEX_ROW; st->gap = 24; st->margin_y = 8; }
    if(class_has(cls, "setting-row")) {
        node->type = HTML_NODE_FLEX_ROW; st->gap = 12; st->padding_y = 10;
        st->bg_color = UI_PANEL; st->border_color = UI_LINE; st->border_radius = 4;
    }
    if(class_has(cls, "settings-sidebar")) {
        st->bg_color = UI_RAISED; st->border_color = UI_LINE; st->border_radius = 4;
        st->padding_x = 8; st->padding_y = 8;
    }
    if(class_has(cls, "settings-content")) { st->padding_x = 8; st->padding_y = 4; }
    if(class_has(cls, "settings-tab")) {
        node->type = HTML_NODE_LINK; st->padding_x = 12; st->padding_y = 8;
        st->margin_y = 1; st->text_color = UI_TEXT;
        if(class_has(cls, "active")) { st->bg_color = 0xffe5f1fbu; st->border_color = UI_ACCENT; st->text_color = UI_ACCENT; st->font_weight = 1; }
    }

    /* Cards & Containers */
    if(class_has(cls, "hero-card")) {
        node->type = HTML_NODE_BLOCK;
        st->bg_color = UI_PANEL;
        st->bg_color_bot = UI_PANEL;
        st->has_gradient = 0;
        st->border_color = UI_LINE;
        st->border_radius = 0;
        st->padding_x = 20;
        st->padding_y = 16;
        st->margin_y = 8;
    } else if(class_has(cls, "card")) {
        node->type = HTML_NODE_BLOCK;
        st->bg_color = UI_PANEL;
        st->bg_color_bot = UI_PANEL;
        st->has_gradient = 0;
        st->border_color = UI_LINE;
        st->border_radius = 0;
        st->padding_x = 16;
        st->padding_y = 12;
        st->margin_y = 6;
    } else if(class_has(cls, "stat-box") || class_has(cls, "metric-card")) {
        node->type = HTML_NODE_STAT_BOX;
        st->bg_color = 0x16ffffffu;
        st->border_color = 0x20ffffffu;
        st->border_radius = 8;
        st->padding_x = 12;
        st->padding_y = 8;
    } else if(class_has(cls, "table")) {
        node->type = HTML_NODE_TABLE;
        st->bg_color = 0x10ffffffu;
        st->border_color = 0x20ffffffu;
        st->border_radius = 8;
        st->margin_y = 8;
    } else if(class_has(cls, "badge")) {
        node->type = HTML_NODE_BADGE;
        st->is_pill = 0;
        st->border_radius = 0;
        st->font_size = 0;
        st->padding_x = 0;
        st->padding_y = 0;
        if(class_has(cls, "bg-success") || class_has(cls, "badge-green")) {
            st->text_color = 0xff107c10u;
        } else if(class_has(cls, "bg-danger") || class_has(cls, "badge-red")) {
            st->text_color = 0xffc42b1cu;
        } else if(class_has(cls, "bg-warning") || class_has(cls, "badge-amber")) {
            st->bg_color = 0x18fbbf24u; st->border_color = 0x30fbbf24u; st->text_color = 0xfffcd34du;
        } else if(class_has(cls, "bg-info") || class_has(cls, "badge-purple")) {
            st->bg_color = 0x18a78bfeu; st->border_color = 0x30a78bfeu; st->text_color = 0xffc4b5fdu;
        } else if(class_has(cls, "bg-primary") || class_has(cls, "badge-blue")) {
            st->text_color = UI_ACCENT;
        } else {
            st->text_color = UI_TEXT;
        }
    } else if(class_has(cls, "btn")) {
        node->type = HTML_NODE_BUTTON;
        st->border_radius = 0;
        st->padding_x = 14;
        st->padding_y = 6;
        if(class_has(cls, "btn-primary")) {
            st->bg_color = 0xff0284c7u;
            st->border_color = 0xff38bdf8u;
            st->text_color = 0xffffffffu;
        } else if(class_has(cls, "btn-success")) {
            st->bg_color = 0xff059669u;
            st->border_color = 0xff10b981u;
            st->text_color = 0xffffffffu;
        } else if(class_has(cls, "btn-info")) {
            st->bg_color = 0xff0284c7u;
            st->border_color = 0xff38bdf8u;
            st->text_color = 0xffffffffu;
        } else {
            st->bg_color = 0xff303030u;
            st->border_color = UI_LINE;
            st->text_color = UI_TEXT;
        }
    } else if(class_has(cls, "progress") || class_has(cls, "meter")) {
        node->type = HTML_NODE_METER;
        st->bg_color = 0xff161b24u;
        st->border_color = 0x20ffffffu;
    } else if(class_has(cls, "alert")) {
        node->type = HTML_NODE_BLOCK;
        st->bg_color = 0x12ffffffu;
        st->border_color = UI_LINE;
        st->border_radius = 8;
        st->padding_x = 14;
        st->padding_y = 10;
        if(class_has(cls, "alert-success")) { st->border_color = 0x4034d399u; st->text_color = 0xff6ee7b7u; }
        else if(class_has(cls, "alert-primary")) { st->border_color = 0x4038bdf8u; st->text_color = 0xff7dd3fcu; }
    }

    if(class_has(cls, "card-title")) { st->font_weight = 1; st->font_size = 1; }
    if(class_has(cls, "card-text")) { st->text_color = UI_MUTED; }
    if(class_has(cls, "text-center")) st->text_align = 1;
    if(class_has(cls, "text-muted")) st->text_color = UI_MUTED;
    if(class_has(cls, "text-accent") || class_has(cls, "text-primary")) st->text_color = UI_ACCENT;
    if(class_has(cls, "text-success")) st->text_color = 0xff4ade80u;
    if(class_has(cls, "text-info")) st->text_color = 0xff38bdf8u;
    if(class_has(cls, "text-bold") || class_has(cls, "fw-bold")) st->font_weight = 1;
    if(class_has(cls, "text-lg") || class_has(cls, "fs-5")) st->font_size = 1;
    if(class_has(cls, "text-xl") || class_has(cls, "fs-4")) st->font_size = 2;
    if(class_has(cls, "text-hero")) st->font_size = 3;
}

/* --- Local page parser --- */

void html_ui_init(void) {
}

int html_ui_parse(const char *html, html_ui_doc_t *doc) {
    kmemset(doc, 0, sizeof(*doc));
    if(!html || !*html) return 0;

    unsigned len = kstrlen(html);
    int cur_parent = -1;

    for(unsigned i = 0; i < len && doc->count < HTML_UI_MAX_NODES;) {
        if(html[i] == '<') {
            if(i + 4 < len && !kstrncmp(html + i, "<!--", 4)) {
                i += 4;
                while(i + 3 < len && kstrncmp(html + i, "-->", 3)) i++;
                i += (i + 3 < len) ? 3 : 0;
                continue;
            }
            i++;
            int is_closing = 0;
            if(i < len && html[i] == '/') { is_closing = 1; i++; }

            char tag_name[16]; unsigned tn = 0;
            while(i < len && ((html[i] >= 'a' && html[i] <= 'z') || (html[i] >= 'A' && html[i] <= 'Z') || (html[i] >= '0' && html[i] <= '9'))) {
                if(tn < 15) tag_name[tn++] = to_lower(html[i]);
                i++;
            }
            tag_name[tn] = 0;

            unsigned tag_attr_start = i;
            char quote = 0;
            while(i < len) {
                char c = html[i];
                if(!quote && c == '>') break;
                if(c == '\'' || c == '\"') {
                    if(!quote) quote = c;
                    else if(quote == c) quote = 0;
                }
                i++;
            }
            unsigned tag_attr_end = i;
            if(i < len) i++;

            if(!tn) continue; /* doctype and other declarations */
            if(is_closing) {
                int ancestor=cur_parent;
                while(ancestor>=0&&!str_equal(doc->nodes[ancestor].tag,tag_name))
                    ancestor=doc->nodes[ancestor].parent;
                if(ancestor>=0) cur_parent=doc->nodes[ancestor].parent;
                continue;
            }

            if(str_equal(tag_name, "script") || str_equal(tag_name, "style") || str_equal(tag_name, "head")) {
                char close_tag[24];
                kstrcpy(close_tag, "</"); kstrcat(close_tag, tag_name);
                unsigned clen = kstrlen(close_tag);
                while(i + clen < len && kstrncmp(html + i, close_tag, clen)) i++;
                while(i < len && html[i] != '>') i++;
                if(i < len) i++;
                continue;
            }

            int nid = doc->count++;
            html_ui_node_t *node = &doc->nodes[nid];
            kstrcpy(node->tag, tag_name);
            node->parent = cur_parent;
            node->meter_val = 50;

            parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "class", node->class_name, sizeof(node->class_name));
            parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "id", node->id, sizeof(node->id));
            parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "href", node->href, sizeof(node->href));
            
            char val_str[16];
            parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "value", val_str, sizeof(val_str));
            if(!val_str[0]) parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "aria-valuenow", val_str, sizeof(val_str));
            if(val_str[0]) {
                int v = 0;
                for(int vi = 0; val_str[vi] >= '0' && val_str[vi] <= '9'; vi++) v = v * 10 + (val_str[vi] - '0');
                node->meter_val = v > 100 ? 100 : v;
            }

            /* Tag Defaults */
            if(str_equal(tag_name, "h1")) { node->type = HTML_NODE_HEADING; node->style.font_size = 2; node->style.font_weight = 1; node->style.margin_y = 6; }
            else if(str_equal(tag_name, "h2")) { node->type = HTML_NODE_HEADING; node->style.font_size = 1; node->style.font_weight = 1; node->style.margin_y = 4; }
            else if(str_equal(tag_name, "h3")) { node->type = HTML_NODE_HEADING; node->style.font_size = 1; node->style.font_weight = 1; node->style.margin_y = 4; }
            else if(str_equal(tag_name, "p") || str_equal(tag_name, "span") || str_equal(tag_name, "small") ||
                    str_equal(tag_name, "strong") || str_equal(tag_name, "b") || str_equal(tag_name, "em") ||
                    str_equal(tag_name, "label")) { node->type = HTML_NODE_TEXT; }
            else if(str_equal(tag_name, "a")) { node->type = HTML_NODE_LINK; node->style.text_color = UI_ACCENT; }
            else if(str_equal(tag_name, "hr")) { node->type = HTML_NODE_HR; node->style.margin_y = 8; }
            else if(str_equal(tag_name, "button")) { node->type = HTML_NODE_BUTTON; }
            else if(str_equal(tag_name, "badge") || str_equal(tag_name, "pill")) { node->type = HTML_NODE_BADGE; node->style.is_pill = 1; }
            else if(str_equal(tag_name, "meter") || str_equal(tag_name, "progress")) { node->type = HTML_NODE_METER; }
            else if(str_equal(tag_name, "table")) { node->type = HTML_NODE_TABLE; }
            else if(str_equal(tag_name, "tr")) { node->type = HTML_NODE_TR; }
            else if(str_equal(tag_name, "th")) { node->type = HTML_NODE_TH; node->style.font_weight = 1; }
            else if(str_equal(tag_name, "td")) { node->type = HTML_NODE_TD; }
            else if(str_equal(tag_name, "li")) { node->type = HTML_NODE_LIST_ITEM; }
            else { node->type = HTML_NODE_BLOCK; }

            apply_css_classes(node);

            /* Inline Style Attribute Parsing */
            char style_attr[96];
            parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "style", style_attr, sizeof(style_attr));
            if(style_attr[0]) {
                if(str_contains(style_attr, "color:")) {
                    const char *p = style_attr;
                    while(*p && kstrncmp(p, "color:", 6)) p++;
                    if(*p) node->style.text_color = parse_hex_color(p + 6, node->style.text_color);
                }
                if(str_contains(style_attr, "background:")) {
                    const char *p = style_attr;
                    while(*p && kstrncmp(p, "background:", 11)) p++;
                    if(*p) node->style.bg_color = parse_hex_color(p + 11, node->style.bg_color);
                }
                if(str_contains(style_attr, "width:")) {
                    const char *p = style_attr;
                    while(*p && kstrncmp(p, "width:", 6)) p++;
                    if(*p) {
                        p += 6; while(*p == ' ') p++;
                        int v = 0;
                        while(*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
                        if(*p == '%' && v > 0) node->meter_val = v > 100 ? 100 : v;
                    }
                }
            }

            /* Read inner text content */
            unsigned tw = 0;
            while(i < len && html[i] != '<') {
                if(tw + 1 < sizeof(node->text)) {
                    char c = html[i];
                    if((uint8_t)c < 32 && c != '\t') c = ' ';
                    node->text[tw++] = c;
                }
                i++;
            }
            while(tw && (node->text[tw-1]==' '||node->text[tw-1]=='\t')) tw--;
            node->text[tw] = 0;

            parse_attribute(html + tag_attr_start, tag_attr_end - tag_attr_start, "data-sub", node->subtext, sizeof(node->subtext));

            if(!str_equal(tag_name, "hr") && !str_equal(tag_name, "br") && !str_equal(tag_name, "img") &&
               !str_equal(tag_name, "input") && !str_equal(tag_name, "meter")) {
                cur_parent = nid;
            }
        } else {
            i++;
        }
    }
    return doc->count;
}

/* --- Layout Engine (deterministic block / flex / table flow) --- */

static void import_string(char *dst,unsigned cap,const char *src) {
    unsigned i=0;while(src[i]&&i+1<cap){dst[i]=src[i];i++;}dst[i]=0;
}
static unsigned bounded_style(int v) { return v<0?0:v>255?255:(unsigned)v; }
void html_ui_import(const aw_node *nodes,int count,html_ui_doc_t *doc) {
    kmemset(doc,0,sizeof(*doc));
    if(count>HTML_UI_MAX_NODES)count=HTML_UI_MAX_NODES;
    doc->count=count;
    for(int i=0;i<count;i++) {
        const aw_node *in=&nodes[i];html_ui_node_t *n=&doc->nodes[i];
        n->parent=in->parent;n->handle=in->handle;n->preferred_width=in->width;n->min_height=in->min_height;
        import_string(n->tag,sizeof(n->tag),in->tag);import_string(n->class_name,sizeof(n->class_name),in->classes);
        import_string(n->id,sizeof(n->id),in->id);import_string(n->text,sizeof(n->text),in->text);import_string(n->href,sizeof(n->href),in->href);
        const char *t=n->tag;
        if(t[0]=='h'&&t[1]>='1'&&t[1]<='6'&&!t[2]){n->type=HTML_NODE_HEADING;n->style.font_size=t[1]=='1'?2:1;}
        else if(str_equal(t,"a"))n->type=HTML_NODE_LINK;
        else if(str_equal(t,"button"))n->type=HTML_NODE_BUTTON;
        else if(str_equal(t,"table"))n->type=HTML_NODE_TABLE;
        else if(str_equal(t,"tr"))n->type=HTML_NODE_TR;
        else if(str_equal(t,"th"))n->type=HTML_NODE_TH;
        else if(str_equal(t,"td"))n->type=HTML_NODE_TD;
        else if(str_equal(t,"hr"))n->type=HTML_NODE_HR;
        else if(n->text[0])n->type=HTML_NODE_TEXT;
        else n->type=HTML_NODE_BLOCK;
        apply_css_classes(n);
        if(in->display==1)n->type=HTML_NODE_FLEX_ROW;
        n->style.text_color=in->color;
        if(in->flags&AW_BG)n->style.bg_color=n->style.bg_color_bot=in->background;
        if(in->flags&AW_BORDER)n->style.border_color=in->border;
        if(in->flags&AW_PADDING){n->style.padding_x=bounded_style(in->padding_x);n->style.padding_y=bounded_style(in->padding_y);}
        if(in->flags&AW_GAP)n->style.gap=bounded_style(in->gap);
        if(in->flags&AW_MARGIN)n->style.margin_y=bounded_style(in->margin);
        n->style.font_size=in->font_size>=24?2:in->font_size>=16?1:0;
        n->style.font_weight=in->weight>=600;n->style.text_align=in->align;
        n->style.border_radius=bounded_style(in->radius);
    }
}

static int html_direct_children(const html_ui_doc_t *doc, int parent) {
    int count = 0;
    for(int i=0;i<doc->count;i++) if(doc->nodes[i].parent==parent) count++;
    return count;
}

/* Return one word-wrapped UTF-8 line; layout and painting share the break. */
static int text_line(const char *text,int width,int size,char out[144]) {
    int n=0, space=-1;
    while(text[n]&&n<139) {
        int end=n+1;
        while(text[end]&&((uint8_t)text[end]&0xc0)==0x80&&end<143) end++;
        for(int i=n;i<end;i++) out[i]=text[i];
        out[end]=0;
        if(ui_text_measure(out,size)>width&&n) {
            if(space>0) n=space;
            break;
        }
        if(text[n]==' ') space=n;
        n=end;
    }
    out[n]=0;
    while(text[n]==' ') n++;
    return n;
}

static int html_leaf_height(const html_ui_node_t *n) {
    if(n->type==HTML_NODE_HR) return 2;
    if(n->type==HTML_NODE_METER) return 10;
    if(n->type==HTML_NODE_BUTTON) return n->style.font_size>0 ? 36 : 32;
    if(n->type==HTML_NODE_BADGE) return 26;
    if(n->type==HTML_NODE_HEADING) return n->style.font_size>=2 ? 34 : 28;
    return n->style.font_size>=2 ? 32 : (n->style.font_size==1 ? 26 : 22);
}

static int html_layout_node(html_ui_doc_t *doc, int idx, int x, int y, int w) {
    html_ui_node_t *n=&doc->nodes[idx];
    html_style_t *st=&n->style;
    int child_count=html_direct_children(doc,idx);
    int pad_x=st->padding_x;
    int pad_y=st->padding_y;
    if(w<1) w=1;
    if(pad_x*2>=w) pad_x=(w-1)/2;
    n->x=x; n->y=y; n->w=w;

    if(n->type==HTML_NODE_HR || n->type==HTML_NODE_METER ||
       n->type==HTML_NODE_BUTTON || n->type==HTML_NODE_BADGE ||
       n->type==HTML_NODE_HEADING || n->type==HTML_NODE_TEXT ||
       n->type==HTML_NODE_LINK || n->type==HTML_NODE_TH || n->type==HTML_NODE_TD) {
        if(n->type==HTML_NODE_BUTTON || n->type==HTML_NODE_BADGE) {
            int tw=ui_text_measure(n->text,st->font_size>0?1:0);
            n->w=tw+st->padding_x*2;
            if(n->w>w) n->w=w;
        }
        n->h=html_leaf_height(n);
        if(n->type==HTML_NODE_TEXT&&n->text[0]) {
            const char *p=n->text; char line[144]; int lines=0;
            int text_w=w-pad_x*2;if(text_w<1)text_w=1;
            while(*p) { p+=text_line(p,text_w,st->font_size>0,line); lines++; }
            n->h=lines*(st->font_size>0?30:24)+8;
        }
        if(n->h<n->min_height)n->h=n->min_height;
        return n->h;
    }

    if(n->type==HTML_NODE_TABLE) {
        int table_y=y+4, row=0;
        for(int i=0;i<doc->count;i++) if(doc->nodes[i].parent==idx && doc->nodes[i].type==HTML_NODE_TR) {
            html_ui_node_t *tr=&doc->nodes[i];
            tr->x=x; tr->y=table_y; tr->w=w; tr->h=38; tr->style.is_striped=(row&1);
            int cells=0; for(int j=0;j<doc->count;j++) if(doc->nodes[j].parent==i && (doc->nodes[j].type==HTML_NODE_TH||doc->nodes[j].type==HTML_NODE_TD)) cells++;
            int cw=cells?w/cells:w, cx=x;
            for(int j=0;j<doc->count;j++) if(doc->nodes[j].parent==i && (doc->nodes[j].type==HTML_NODE_TH||doc->nodes[j].type==HTML_NODE_TD)) {
                doc->nodes[j].x=cx; doc->nodes[j].y=table_y; doc->nodes[j].w=cw; doc->nodes[j].h=38; cx+=cw;
            }
            table_y+=38; row++;
        }
        n->h=table_y-y+pad_y*2;
        return n->h;
    }

    if(n->type==HTML_NODE_FLEX_ROW) {
        int gap=st->gap?st->gap:10, count=child_count;
        int inner_w=w-pad_x*2; if(inner_w<1) inner_w=1;
        int fixed=0,fluid=0;
        for(int i=0;i<doc->count;i++)if(doc->nodes[i].parent==idx){if(doc->nodes[i].preferred_width>0)fixed+=doc->nodes[i].preferred_width;else fluid++;}
        int item_w=count ? (inner_w-gap*(count-1)-fixed)/(fluid?fluid:count) : inner_w;
        int cx=x+pad_x, cy=y+pad_y, max_h=0;
        int stacked=item_w<160;
        if(stacked) item_w=inner_w;
        for(int i=0;i<doc->count;i++) if(doc->nodes[i].parent==idx) {
            int cw=item_w;
            if(!stacked&&doc->nodes[i].preferred_width>0)cw=doc->nodes[i].preferred_width;
            else if(!fixed&&!stacked&&doc->nodes[i].style.col_span>0 && doc->nodes[i].style.col_span<=12)
                cw=(inner_w*doc->nodes[i].style.col_span)/12-gap;
            if(cw<1) cw=1;
            if(cw>inner_w) cw=inner_w;
            if(cx>x+pad_x&&cx+cw>x+pad_x+inner_w) {
                cy+=max_h+gap; cx=x+pad_x; max_h=0;
            }
            int ch=html_layout_node(doc,i,cx,cy,cw);
            if(ch>max_h) max_h=ch;
            cx+=cw+gap;
        }
        n->h=cy-y+max_h+pad_y;
        return n->h;
    }

    if(child_count>0) {
        int cy=y+pad_y;
        for(int i=0;i<doc->count;i++) if(doc->nodes[i].parent==idx) {
            html_ui_node_t *c=&doc->nodes[i];
            cy+=c->style.margin_y;
            html_layout_node(doc,i,x+pad_x,cy,w-pad_x*2);
            cy=c->y+c->h+c->style.margin_y;
        }
        n->h=(cy-y)+pad_y;
        if(n->text[0] && n->h<26) n->h=26;
    } else {
        n->h=n->text[0] ? html_leaf_height(n)+pad_y*2 : pad_y*2;
    }
    return n->h;
}

void html_ui_layout(html_ui_doc_t *doc, int max_w, int max_h) {
    (void)max_h;
    if(!doc || doc->count==0) return;
    int content_w=max_w-40; if(content_w<1) content_w=1;
    int cur_y=12;
    for(int i=0;i<doc->count;i++) if(doc->nodes[i].parent<0) {
        html_ui_node_t *n=&doc->nodes[i];
        cur_y+=n->style.margin_y;
        html_layout_node(doc,i,20,cur_y,content_w);
        cur_y=n->y+n->h+n->style.margin_y;
    }
    doc->content_height=cur_y+20;
}

/* --- Render Engine --- */

void html_ui_render(gpu_surface_t *surf, int offset_x, int offset_y, int scroll_y, const html_ui_doc_t *doc) {
    if(!surf || !doc || doc->count == 0) return;

    for(int i = 0; i < doc->count; i++) {
        const html_ui_node_t *n = &doc->nodes[i];
        const html_style_t *st = &n->style;

        int draw_x = offset_x + n->x;
        int draw_y = offset_y + n->y - scroll_y;

        /* Clipping check */
        if(draw_y + n->h < offset_y || draw_y > offset_y + (int)surf->h) continue;

        if(n->type == HTML_NODE_HR) {
            gpu_fill_rect(surf, draw_x, draw_y, n->w, 1, UI_LINE);
            continue;
        }

        if(n->type == HTML_NODE_TR) {
            if(st->is_striped) {
                gpu_fill_rect(surf, draw_x, draw_y, n->w, n->h, 0x14ffffffu);
            }
            gpu_fill_rect(surf, draw_x, draw_y + n->h - 1, n->w, 1, 0x18ffffffu);
            continue;
        }

        if(n->type == HTML_NODE_TH) {
            gpu_fill_rect(surf, draw_x, draw_y, n->w, n->h, 0x301e293bu);
            gpu_fill_rect(surf, draw_x, draw_y + n->h - 1, n->w, 1, 0x4038bdf8u);
            if(n->text[0]) {
                ui_text_fit(surf, draw_x + 8, draw_y + 6, n->w - 16, n->text, 0xff7dd3fcu, 0);
            }
            continue;
        }

        if(n->type == HTML_NODE_TD) {
            if(n->text[0]) {
                ui_text_fit(surf, draw_x + 8, draw_y + 6, n->w - 16, n->text, UI_TEXT, 0);
            }
            continue;
        }

        if(n->type == HTML_NODE_METER) {
            html_render_meter(surf, draw_x, draw_y, n->w, n->h, n->meter_val, 0xff38bdf8u);
            continue;
        }

        /* Render Card / Box Background */
        if(st->bg_color) {
            if(st->is_pill) {
                html_render_pill(surf, draw_x, draw_y, n->w, n->h, st->bg_color, st->border_color);
            } else {
                html_render_card(surf, draw_x, draw_y, n->w, n->h, st->border_radius,
                                 st->bg_color, st->has_gradient ? st->bg_color_bot : st->bg_color, st->border_color);
            }
        }

        /* Render Text */
        if(n->text[0]) {
            int tx = draw_x + st->padding_x;
            int ty = draw_y + (st->padding_y ? st->padding_y : 4);
            int avail_w = n->w - st->padding_x * 2;

            if(st->text_align == 1) {
                int tw = ui_text_measure(n->text, st->font_size > 0 ? 1 : 0);
                tx = draw_x + (n->w - tw) / 2;
            }

            int size=st->font_size>0?1:0;
            if(n->type==HTML_NODE_TEXT) {
                const char *p=n->text; char line[144];
                while(*p) {
                    p+=text_line(p,avail_w,size,line);
                    ui_text_fit(surf,tx,ty,avail_w,line,st->text_color,size);
                    ty+=size?30:24;
                }
            } else ui_text_fit(surf,tx,ty,avail_w,n->text,st->text_color,size);

            if(n->subtext[0]) {
                ui_text_fit(surf, tx, ty + 20, avail_w, n->subtext, UI_MUTED, 0);
            }
        }
    }
}

/* --- Hit Test / Click Handler --- */

int html_ui_click(const html_ui_doc_t *doc, int click_x, int click_y, int scroll_y, char *out_action, unsigned cap) {
    if(!doc || !out_action || cap == 0) return 0;
    out_action[0] = 0;
    int page_y = click_y + scroll_y;

    for(int i = 0; i < doc->count; i++) {
        const html_ui_node_t *n = &doc->nodes[i];
        if(click_x >= n->x && click_x <= n->x + n->w && page_y >= n->y && page_y <= n->y + n->h) {
            if(n->href[0]) {
                unsigned len = 0;
                while(n->href[len] && len + 1 < cap) { out_action[len] = n->href[len]; len++; }
                out_action[len] = 0;
                return 1;
            }
        }
    }
    return 0;
}

/* These pages are generated from assets/ui by the build. */
const char *html_ui_get_welcome_page(void) { return embedded_ui_welcome; }
const char *html_ui_get_system_page(void) { return embedded_ui_system; }
const char *html_ui_get_network_page(void) { return embedded_ui_network; }
const char *html_ui_get_about_page(void) { return embedded_ui_about; }
const char *html_ui_get_files_page(void) { return embedded_ui_files; }
const char *html_ui_get_bluetooth_page(void) { return embedded_ui_bluetooth; }
const char *html_ui_get_bootstrap_css(void) { return embedded_ui_css; }
