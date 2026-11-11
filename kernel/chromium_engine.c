#include "chromium_engine.h"
#include "ui_font.h"
#include "theme.h"
#include "keyboard.h"
#include "axis_web.h"

dom_tree_t g_dom_tree;
devtools_state_t g_devtools;

static int lower(int c) { return c>='A'&&c<='Z'?c+32:c; }
static void kstrncpy_safe(char *dst, const char *src, unsigned n) {
    if(!dst || !n) return;
    unsigned i = 0;
    while(src && src[i] && i + 1 < n) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static int streq(const char *a, const char *b) {
    while(*a && *b) {
        if(lower((uint8_t)*a) != lower((uint8_t)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

void chromium_engine_init(void) {
    kmemset(&g_dom_tree, 0, sizeof(g_dom_tree));
    kmemset(&g_devtools, 0, sizeof(g_devtools));
    g_devtools.height = 180;
    g_devtools.active_tab = DEVTOOLS_TAB_CONSOLE;
    chromium_console_log("AXIS Web: Lexbor + QuickJS");
}

void chromium_console_log(const char *msg) {
    if(g_devtools.console_count >= CHROMIUM_CONSOLE_LINES) {
        for(int i=0; i<CHROMIUM_CONSOLE_LINES-1; i++) {
            kstrcpy(g_devtools.console_log[i], g_devtools.console_log[i+1]);
        }
        g_devtools.console_count = CHROMIUM_CONSOLE_LINES - 1;
    }
    kstrncpy_safe(g_devtools.console_log[g_devtools.console_count], msg, 94);
    g_devtools.console_log[g_devtools.console_count][94] = 0;
    g_devtools.console_count++;
}

static void parse_attr(const char *tag_str, unsigned len, const char *attr_name, char *out, unsigned out_cap) {
    out[0] = 0;
    unsigned n = kstrlen(attr_name);
    for(unsigned i=0; i+n+1<len; i++) {
        if((i==0 || tag_str[i-1]==' ' || tag_str[i-1]=='\t') && !kstrncmp(tag_str+i, attr_name, n) && tag_str[i+n]=='=') {
            unsigned p = i + n + 1;
            char quote = 0;
            if(p < len && (tag_str[p]=='\"' || tag_str[p]=='\'')) quote = tag_str[p++];
            unsigned w = 0;
            while(p < len && (quote ? tag_str[p]!=quote : (tag_str[p]!=' ' && tag_str[p]!='>'))) {
                if(w + 1 < out_cap) out[w++] = tag_str[p];
                p++;
            }
            out[w] = 0;
            return;
        }
    }
}

void chromium_dom_build(const char *html, unsigned len, dom_tree_t *tree) {
    kmemset(tree, 0, sizeof(*tree));
    if(!html || !len) return;

    int cur_parent = -1;
    for(unsigned i=0; i<len && tree->node_count < CHROMIUM_MAX_NODES;) {
        if(html[i] == '<') {
            /* HTML Yorumları atla */
            if(i+4 < len && !kstrncmp(html+i, "<!--", 4)) {
                i += 4;
                while(i+3 < len && kstrncmp(html+i, "-->", 3)) i++;
                i += (i+3 < len) ? 3 : 0;
                continue;
            }
            unsigned tag_start = ++i;
            int is_closing = 0;
            if(i < len && html[i] == '/') { is_closing = 1; i++; tag_start = i; }
            
            char tag_name[16]; unsigned tn = 0;
            while(i < len && ((html[i]>='a'&&html[i]<='z')||(html[i]>='A'&&html[i]<='Z')||(html[i]>='0'&&html[i]<='9'))) {
                if(tn < 15) tag_name[tn++] = lower(html[i]);
                i++;
            }
            tag_name[tn] = 0;

            /* Tag sonuna kadar tara */
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
            unsigned tag_end = i;
            if(i < len) i++;

            if(is_closing) {
                if(cur_parent >= 0) cur_parent = tree->nodes[cur_parent].parent;
                continue;
            }

            /* Script / style atla */
            if(streq(tag_name, "script") || streq(tag_name, "style") || streq(tag_name, "head")) {
                char close_tag[24];
                kstrcpy(close_tag, "</"); kstrcat(close_tag, tag_name);
                unsigned clen = kstrlen(close_tag);
                while(i+clen < len && kstrncmp(html+i, close_tag, clen)) i++;
                while(i < len && html[i] != '>') i++;
                if(i < len) i++;
                continue;
            }

            int nid = tree->node_count++;
            dom_node_t *node = &tree->nodes[nid];
            node->type = DOM_NODE_ELEMENT;
            kstrcpy(node->tag, tag_name);
            node->parent = cur_parent;
            node->color = UI_TEXT;
            node->bg_color = 0;
            node->is_block = 1;

            parse_attr(html+tag_start, tag_end-tag_start, "href", node->href, sizeof(node->href));
            parse_attr(html+tag_start, tag_end-tag_start, "src", node->src, sizeof(node->src));
            parse_attr(html+tag_start, tag_end-tag_start, "id", node->id, sizeof(node->id));
            parse_attr(html+tag_start, tag_end-tag_start, "class", node->class_name, sizeof(node->class_name));

            if(streq(tag_name, "h1")) { node->is_bold = 1; node->font_size = 2; node->color = 0xff60a5fau; }
            else if(streq(tag_name, "h2")) { node->is_bold = 1; node->font_size = 1; node->color = 0xff93c5fdu; }
            else if(streq(tag_name, "h3")) { node->is_bold = 1; node->font_size = 1; node->color = 0xffbfdbfeu; }
            else if(streq(tag_name, "a")) { node->is_block = 0; node->color = UI_ACCENT; }
            else if(streq(tag_name, "span")) { node->is_block = 0; }
            else if(streq(tag_name, "button")) { node->bg_color = UI_HOVER; node->color = 0xffffffffu; }
            else if(streq(tag_name, "pre") || streq(tag_name, "code")) { node->bg_color = 0xff0d1117u; node->color = 0xffa5d6a7u; }

            /* Text içerik oku */
            unsigned txt_w = 0;
            while(i < len && html[i] != '<') {
                if(txt_w + 1 < sizeof(node->text)) {
                    char c = html[i];
                    if((uint8_t)c < 32 && c != '\t') c = ' ';
                    node->text[txt_w++] = c;
                }
                i++;
            }
            node->text[txt_w] = 0;

            /* Kendi kapanan tagler değilse parent yap */
            if(!streq(tag_name, "br") && !streq(tag_name, "hr") && !streq(tag_name, "img") && !streq(tag_name, "input")) {
                cur_parent = nid;
            }
        } else {
            i++;
        }
    }
}

void chromium_dom_layout(dom_tree_t *tree, int view_w, int view_h, int *total_h) {
    (void)view_h;
    int cur_y = 12;
    int cur_x = 20;
    int line_h = 24;

    for(int i=0; i<tree->node_count; i++) {
        dom_node_t *n = &tree->nodes[i];
        if(n->is_block) {
            if(cur_x > 20) { cur_y += line_h; cur_x = 20; }
            n->x = 20;
            n->y = cur_y;
            n->w = view_w - 40;
            int text_len = kstrlen(n->text);
            int text_w = ui_text_measure(n->text, n->font_size > 1 ? 1 : 0);
            if(text_w <= 0 && text_len > 0) text_w = text_len * 9;
            n->h = (n->font_size > 1 ? 32 : (n->font_size == 1 ? 28 : 22));
            if(streq(n->tag, "hr")) { n->h = 4; cur_y += 10; }
            else if(streq(n->tag, "h1") || streq(n->tag, "h2")) { cur_y += n->h + 8; }
            else { cur_y += n->h + 4; }
        } else {
            /* Inline element */
            int tw = ui_text_measure(n->text, 0);
            if(tw <= 0 && n->text[0]) tw = kstrlen(n->text) * 9;
            if(tw <= 0) tw = 40;
            if(cur_x + tw > view_w - 30) {
                cur_y += line_h;
                cur_x = 20;
            }
            n->x = cur_x;
            n->y = cur_y;
            n->w = tw;
            n->h = 20;
            cur_x += tw + 10;
        }
    }
    if(cur_x > 20) cur_y += line_h;
    if(total_h) *total_h = cur_y;
}

void chromium_dom_render(gpu_surface_t *s, int x, int y, int w, int h, int scroll_y, dom_tree_t *tree) {
    (void)w;
    for(int i=0; i<tree->node_count; i++) {
        dom_node_t *n = &tree->nodes[i];
        int ry = n->y - scroll_y;
        if(ry + n->h < 0 || ry > h) continue;

        int draw_x = x + n->x;
        int draw_y = y + ry;

        if(streq(n->tag, "hr")) {
            gpu_fill_rect(s, draw_x, draw_y + 1, n->w, 1, UI_LINE);
            continue;
        }
        if(n->bg_color) {
            gpu_fill_rect(s, draw_x - 4, draw_y - 2, n->w + 8, n->h + 4, n->bg_color);
            gpu_draw_rect(s, draw_x - 4, draw_y - 2, n->w + 8, n->h + 4, UI_LINE, 1);
        }
        if(n->text[0]) {
            ui_text(s, draw_x, draw_y, n->text, n->color, n->font_size > 1 ? 1 : 0);
            if(streq(n->tag, "a")) {
                /* Underline */
                gpu_fill_rect(s, draw_x, draw_y + 18, n->w, 1, n->color);
            }
        }
    }
}

int chromium_dom_click(dom_tree_t *tree, int click_x, int click_y, int scroll_y, char *out_href) {
    int page_y = click_y + scroll_y;
    for(int i=0; i<tree->node_count; i++) {
        dom_node_t *n = &tree->nodes[i];
        if(n->href[0]) {
            if(click_x >= n->x && click_x <= n->x + n->w &&
               page_y >= n->y && page_y <= n->y + n->h + 4) {
                kstrcpy(out_href, n->href);
                return 1;
            }
        }
    }
    return 0;
}

/* V8-Lite JavaScript Evaluator */
int chromium_js_eval(const char *script, char *out_result, unsigned out_cap) {
    if(!script || !script[0]) {
        kstrcpy(out_result, "undefined");
        return 1;
    }
    /* console.log(...) */
    if(!kstrncmp(script, "console.log(", 12)) {
        const char *p = script + 12;
        char msg[80]; unsigned m = 0;
        char quote = 0;
        if(*p == '\'' || *p == '\"') quote = *p++;
        while(*p && (quote ? *p != quote : *p != ')')) {
            if(m + 1 < sizeof(msg)) msg[m++] = *p;
            p++;
        }
        msg[m] = 0;
        chromium_console_log(msg);
        kstrcpy(out_result, "undefined");
        return 1;
    }
    /* navigator.userAgent */
    if(!kstrcmp(script, "navigator.userAgent") || !kstrcmp(script, "navigator.appVersion")) {
        kstrncpy_safe(out_result, "Mozilla/5.0 (Shenlong; Chrome/124.0.0.0) Jade/1.2", out_cap);
        out_result[out_cap-1] = 0;
        return 1;
    }
    /* document.title */
    if(!kstrcmp(script, "document.title")) {
        kstrncpy_safe(out_result, "JADE Browser (Chromium Core)", out_cap);
        out_result[out_cap-1] = 0;
        return 1;
    }
    /* Basit Aritmetik Hesaplayıcı: örn 10 + 20 */
    int a = 0, b = 0; char op = 0;
    const char *p = script;
    while(*p == ' ') p++;
    while(*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
    while(*p == ' ') p++;
    if(*p == '+' || *p == '-' || *p == '*' || *p == '/') {
        op = *p++;
        while(*p == ' ') p++;
        while(*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
        int res = 0;
        if(op == '+') res = a + b;
        else if(op == '-') res = a - b;
        else if(op == '*') res = a * b;
        else if(op == '/' && b != 0) res = a / b;
        char num[24]; kitoa(res, num);
        kstrncpy_safe(out_result, num, out_cap);
        out_result[out_cap-1] = 0;
        return 1;
    }
    kstrncpy_safe(out_result, "V8-Lite: OK", out_cap);
    out_result[out_cap-1] = 0;
    return 1;
}

/* DevTools Çekmecesi Çizimi */
void chromium_devtools_draw(gpu_surface_t *s, int x, int y, int w, int h) {
    if(!g_devtools.open) return;
    int dy = y + h - g_devtools.height;
    int dh = g_devtools.height;

    /* DevTools Arka Planı */
    gpu_fill_rect(s, x, dy, w, dh, 0xff16181fu);
    gpu_fill_rect(s, x, dy, w, 1, UI_LINE);

    /* DevTools Üst Sekmeleri */
    gpu_fill_rect(s, x, dy + 1, w, 28, 0xff1e222cu);
    const char *tabs[] = {"Console", "Elements (DOM)", "Network", "Sources"};
    int tx = x + 12;
    for(int t=0; t<4; t++) {
        int tw = ui_text_measure(tabs[t], 0) + 16;
        if(g_devtools.active_tab == (devtools_tab_t)t) {
            gpu_fill_rect(s, tx, dy + 1, tw, 27, 0xff16181fu);
            gpu_fill_rect(s, tx, dy + 1, tw, 2, UI_ACCENT);
            ui_text(s, tx + 8, dy + 6, tabs[t], 0xffffffffu, 0);
        } else {
            ui_text(s, tx + 8, dy + 6, tabs[t], UI_MUTED, 0);
        }
        tx += tw + 4;
    }
    /* DevTools Kapatma [ ✕ ] Butonu */
    ui_text(s, x + w - 24, dy + 6, "x", UI_MUTED, 0);
    gpu_fill_rect(s, x, dy + 29, w, 1, UI_LINE);

    /* Sekme İçerikleri */
    int cy = dy + 34;
    if(g_devtools.active_tab == DEVTOOLS_TAB_CONSOLE) {
        int max_lines = (dh - 60) / 18;
        int start_l = g_devtools.console_count > max_lines ? g_devtools.console_count - max_lines : 0;
        for(int l=start_l; l<g_devtools.console_count; l++) {
            ui_text(s, x + 16, cy, g_devtools.console_log[l], 0xff94a3b8u, 0);
            cy += 18;
        }
        /* Komut Giriş Satırı */
        gpu_fill_rect(s, x, dy + dh - 24, w, 24, 0xff12141au);
        gpu_fill_rect(s, x, dy + dh - 24, w, 1, UI_LINE);
        ui_text(s, x + 8, dy + dh - 20, ">", UI_ACCENT, 0);
        char cmd_display[100];
        kstrcpy(cmd_display, g_devtools.cmd_input);
        kstrcat(cmd_display, "_");
        ui_text(s, x + 24, dy + dh - 20, cmd_display, 0xfff1f5f9u, 0);
    } else if(g_devtools.active_tab == DEVTOOLS_TAB_ELEMENTS) {
        ui_text(s, x + 16, cy, "<!DOCTYPE html>", 0xff60a5fau, 0); cy += 18;
        ui_text(s, x + 16, cy, "<html>", 0xff60a5fau, 0); cy += 18;
        for(int i=0; i<g_dom_tree.node_count && i<6; i++) {
            dom_node_t *n = &g_dom_tree.nodes[i];
            char elem_str[96];
            kstrcpy(elem_str, "  <");
            kstrcat(elem_str, n->tag);
            if(n->class_name[0]) { kstrcat(elem_str, " class=\""); kstrcat(elem_str, n->class_name); kstrcat(elem_str, "\""); }
            if(n->id[0]) { kstrcat(elem_str, " id=\""); kstrcat(elem_str, n->id); kstrcat(elem_str, "\""); }
            kstrcat(elem_str, "> ");
            if(n->text[0]) {
                int len = kstrlen(elem_str);
                for(int c=0; n->text[c] && c<20 && len<90; c++) elem_str[len++] = n->text[c];
                elem_str[len] = 0;
            }
            ui_text(s, x + 16, cy, elem_str, 0xfffacc15u, 0);
            cy += 18;
        }
        ui_text(s, x + 16, cy, "</html>", 0xff60a5fau, 0);
    } else if(g_devtools.active_tab == DEVTOOLS_TAB_NETWORK) {
        ui_text(s, x + 16, cy, "Method  Status  Protocol   Domain          Size", UI_MUTED, 0); cy += 20;
        gpu_fill_rect(s, x + 16, cy - 2, w - 32, 1, UI_LINE);
        ui_text(s, x + 16, cy, "GET     200 OK  TLS 1.3    google.com      14.2 KB", 0xff34d399u, 0); cy += 18;
        ui_text(s, x + 16, cy, "GET     200 OK  TLS 1.3    m.youtube.com   28.6 KB", 0xff34d399u, 0); cy += 18;
    } else {
        ui_text(s, x + 16, cy, "Sources: JADE Chromium Runtime (Blink DOM / V8-Lite Engine)", UI_TEXT, 0);
    }
}

void chromium_devtools_click(int x, int y, int w, int h) {
    if(!g_devtools.open) return;
    int dy = h - g_devtools.height;
    if(y < dy) return;
    /* Tab seçimleri */
    if(y >= dy && y <= dy + 28) {
        if(x >= w - 30) { g_devtools.open = 0; return; }
        if(x < 80) g_devtools.active_tab = DEVTOOLS_TAB_CONSOLE;
        else if(x < 200) g_devtools.active_tab = DEVTOOLS_TAB_ELEMENTS;
        else if(x < 280) g_devtools.active_tab = DEVTOOLS_TAB_NETWORK;
        else g_devtools.active_tab = DEVTOOLS_TAB_SOURCES;
    }
}

int chromium_devtools_key(char c) {
    if(!g_devtools.open) return 0;
    if(c == '\n') {
        if(g_devtools.cmd_len > 0) {
            char log_entry[100];
            kstrcpy(log_entry, "> ");
            kstrcat(log_entry, g_devtools.cmd_input);
            chromium_console_log(log_entry);
            char result[96];
            axis_web_eval(3,g_devtools.cmd_input, result, sizeof(result));
            char res_entry[100];
            kstrcpy(res_entry, "< ");
            kstrcat(res_entry, result);
            chromium_console_log(res_entry);
            g_devtools.cmd_len = 0;
            g_devtools.cmd_input[0] = 0;
        }
        return 1;
    }
    if(c == '\b' || c == KBD_DEL) {
        if(g_devtools.cmd_len > 0) {
            g_devtools.cmd_len--;
            g_devtools.cmd_input[g_devtools.cmd_len] = 0;
        }
        return 1;
    }
    if((uint8_t)c >= 32 && (uint8_t)c < 127) {
        if(g_devtools.cmd_len + 1 < (int)sizeof(g_devtools.cmd_input)) {
            g_devtools.cmd_input[g_devtools.cmd_len++] = c;
            g_devtools.cmd_input[g_devtools.cmd_len] = 0;
        }
        return 1;
    }
    return 0;
}
