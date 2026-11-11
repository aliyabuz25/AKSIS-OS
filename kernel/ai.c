/*
 * AXIS OS - Native AI Assistant & Copilot (OpenRouter Integration)
 * Akıcı Genişletilmiş Offcanvas (520px), Çok Turlu Gerçek Sohbet Motoru
 * Modern, Ferah, Dinamik Boyutlu & Sıkışmayan Chat Balonları
 */

#include "ai.h"
#include "theme.h"
#include "ui_font.h"
#include "ui_paint.h"
#include "keyboard.h"
#include "net.h"
#include "browser.h"
#include "clock.h"
#include "kernel_info.h"

extern void ui_open_app(int app);
extern void ui_reboot(void);
extern void ui_open_browser(const char *url);

static inline const char *str_find(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!needle[0]) return haystack;
    for (int i = 0; haystack[i]; i++) {
        int match = 1;
        for (int j = 0; needle[j]; j++) {
            if (haystack[i + j] != needle[j]) { match = 0; break; }
        }
        if (match) return haystack + i;
    }
    return NULL;
}

static inline void str_copy_n(char *dst, const char *src, unsigned n) {
    if (!dst || !n) return;
    unsigned i = 0;
    if (src) {
        while (src[i] && i < n - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

int            ai_open = 0;
int            ai_anim = 0;
int            ai_closing = 0;
int            ai_x = 0;
uint64_t       ai_started = 0;
gpu_surface_t *ai_surface = NULL;
int            ai_input_focus = 1;
char           ai_input[AI_INPUT_MAX] = {0};
int            ai_input_len = 0;

static ai_msg_t s_msgs[AI_MAX_MSGS];
static int      s_msg_count = 0;
static int      s_busy = 0;
static uint64_t s_busy_started = 0;
static int      s_querying_net = 0;

static const char OPENROUTER_API_KEY[] = "YOUR_OPENROUTER_API_KEY";
static const char OPENROUTER_MODEL[]   = "openrouter/free";

static void add_message(const char *sender, const char *text, int is_user) {
    if (s_msg_count >= AI_MAX_MSGS) {
        for (int i = 0; i < AI_MAX_MSGS - 1; i++) {
            s_msgs[i] = s_msgs[i + 1];
        }
        s_msg_count = AI_MAX_MSGS - 1;
    }
    ai_msg_t *m = &s_msgs[s_msg_count++];
    str_copy_n(m->sender, sender, sizeof(m->sender));
    str_copy_n(m->text, text, sizeof(m->text));
    m->is_user = is_user;
}

void ai_init(void) {
    s_msg_count = 0;
    add_message("Axis AI",
        "Merhaba Ali! Ben AXIS OS akıllı asistanınızım. Dilediğiniz her konuda sohbet edebilir, soru sorabilir veya sisteminizi yönetebilirsiniz.",
        0);
}

void ai_open_drawer(void) {
    if (ai_open && !ai_closing) return;
    ai_open = 1;
    ai_closing = 0;
    ai_anim = 1;
    ai_started = get_uptime_ms();
    ai_input_focus = 1;
    if (ai_surface) ai_surface->visible = 1;
}

void ai_close_drawer(void) {
    if (!ai_open || ai_closing) return;
    ai_closing = 1;
    ai_anim = 1;
    ai_started = get_uptime_ms();
}

void ai_toggle(void) {
    if (ai_open && !ai_closing) ai_close_drawer();
    else ai_open_drawer();
}

/* Çizim: Çok renkli akıcı dairesel AI Copilot ikonu (Görev çubuğu ve Başlat için) */
void ai_draw_circle_icon(gpu_surface_t *s, int cx, int cy, int radius, int hot) {
    if (!s || radius <= 0) return;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= radius * radius) {
                int ratio = (dy + radius) * 255 / (radius * 2);
                uint8_t r = (uint8_t)(6 + ratio * 133 / 255);
                uint8_t g = (uint8_t)(182 - ratio * 90 / 255);
                uint8_t b = (uint8_t)(212 + ratio * 34 / 255);
                uint32_t col = 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                gpu_put_pixel(s, cx + dx, cy + dy, col);
            }
        }
    }
    int ir = radius - 3;
    if (ir > 2) {
        for (int dy = -ir; dy <= ir; dy++) {
            for (int dx = -ir; dx <= ir; dx++) {
                int d2 = dx * dx + dy * dy;
                if (d2 <= ir * ir) {
                    uint32_t inner_col = hot ? 0xffffffffu : 0xfff0f9ffu;
                    gpu_put_pixel(s, cx + dx, cy + dy, inner_col);
                }
            }
        }
    }
    uint32_t spark_col = hot ? 0xff0284c7u : 0xff0ea5e9u;
    for (int d = -4; d <= 4; d++) {
        gpu_put_pixel(s, cx + d, cy, spark_col);
        gpu_put_pixel(s, cx, cy + d, spark_col);
    }
    gpu_put_pixel(s, cx - 1, cy - 1, spark_col);
    gpu_put_pixel(s, cx + 1, cy - 1, spark_col);
    gpu_put_pixel(s, cx - 1, cy + 1, spark_col);
    gpu_put_pixel(s, cx + 1, cy + 1, spark_col);
}

/* Sistem Eylemlerini Yürütme */
static void execute_action(const char *action) {
    if (!kstrcmp(action, "OPEN_FILES")) {
        ui_open_app(1);
    } else if (!kstrcmp(action, "OPEN_SETTINGS")) {
        ui_open_app(2);
    } else if (!kstrcmp(action, "OPEN_BROWSER")) {
        ui_open_app(3);
    } else if (!kstrcmp(action, "OPEN_TERMINAL")) {
        ui_open_app(0);
    } else if (!kstrcmp(action, "OPEN_MEDIA")) {
        ui_open_app(5);
    } else if (!kstrcmp(action, "OPEN_NETWORK")) {
        ui_open_app(4);
    } else if (!kstrcmp(action, "REBOOT")) {
        ui_reboot();
    }
}

/* Aksiyon etiketini metinden temizleme */
static void strip_action(char *str, const char *act) {
    if (!str || !act) return;
    char *p = (char *)str_find(str, act);
    if (!p) return;
    int act_len = kstrlen(act);
    int rem_len = kstrlen(p + act_len);
    for (int i = 0; i <= rem_len; i++) {
        p[i] = p[i + act_len];
    }
    int len = kstrlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[--len] = 0;
    }
}

/* Yerel Hızlı Komut Kontrolü (Ağ kapalıyken veya kısayollar için) */
static int handle_local_query(const char *q, char *resp, unsigned max_len) {
    char lower[128];
    unsigned len = 0;
    while (q[len] && len < 127) {
        char c = q[len];
        if (c >= 'A' && c <= 'Z') c += 32;
        lower[len] = c;
        len++;
    }
    lower[len] = 0;

    if (str_find(lower, "dosya") || str_find(lower, "klasor") || str_find(lower, "disk")) {
        execute_action("OPEN_FILES");
        str_copy_n(resp, "Dosya Gezgini açıldı. /home ve disklerinizi yönetebilirsiniz.", max_len);
        return 1;
    }
    if (str_find(lower, "ayar") || str_find(lower, "donanim")) {
        execute_action("OPEN_SETTINGS");
        str_copy_n(resp, "Sistem ve Donanım Ayarları açıldı.", max_len);
        return 1;
    }
    if (str_find(lower, "tarayici") || str_find(lower, "browser") || str_find(lower, "web")) {
        execute_action("OPEN_BROWSER");
        str_copy_n(resp, "Jade HTML5 Web Tarayıcısı açıldı.", max_len);
        return 1;
    }
    if (str_find(lower, "terminal") || str_find(lower, "konsol") || str_find(lower, "shell")) {
        execute_action("OPEN_TERMINAL");
        str_copy_n(resp, "x86_64 Long Mode Terminal kabuğu açıldı.", max_len);
        return 1;
    }
    if (str_find(lower, "medya") || str_find(lower, "video") || str_find(lower, "muzik")) {
        execute_action("OPEN_MEDIA");
        str_copy_n(resp, "Medya oynatıcı açıldı.", max_len);
        return 1;
    }
    if (str_find(lower, "yeniden baslat") || str_find(lower, "reboot")) {
        str_copy_n(resp, "AXIS OS güvenli şekilde yeniden başlatılıyor...", max_len);
        ui_reboot();
        return 1;
    }
    return 0;
}

/* JSON Metin Kaçırma */
static void append_escaped_json(char *buf, unsigned cap, const char *text) {
    unsigned blen = kstrlen(buf);
    for (unsigned i = 0; text[i] && blen < cap - 10; i++) {
        char c = text[i];
        if (c == '"' || c == '\\') {
            if (blen < cap - 2) { buf[blen++] = '\\'; buf[blen++] = c; }
        } else if (c == '\n') {
            if (blen < cap - 2) { buf[blen++] = '\\'; buf[blen++] = 'n'; }
        } else if (c == '\r') {
            /* atla */
        } else if (c == '\t') {
            if (blen < cap - 1) buf[blen++] = ' ';
        } else {
            buf[blen++] = c;
        }
    }
    buf[blen] = 0;
}

/* OpenRouter Çok Turlu (Multi-turn Contextual) API İsteği Başlatma */
static int start_openrouter_request(void) {
    if (!net_info.present || !net_info.enabled || !net_info.link) return 0;

    static char payload[3584];
    static char req[4608];

    /* JSON Başlangıcı ve Sistem İstemi */
    kstrcpy(payload, "{\"model\":\"");
    kstrcat(payload, OPENROUTER_MODEL);
    kstrcat(payload, "\",\"max_tokens\":300,\"messages\":[{\"role\":\"system\",\"content\":\"Sen AXIS OS Turkce isletim sistemi asistanisin. Ali ile cana yakin, zeki, akici ve samimi sohbet et. Kisa ve net konus. Sistem islemleri gerekiyorsa metnine [ACTION:OPEN_FILES], [ACTION:OPEN_SETTINGS], [ACTION:OPEN_TERMINAL], [ACTION:OPEN_BROWSER], [ACTION:OPEN_MEDIA] veya [ACTION:REBOOT] ekle.\"}");

    /* Önceki mesaj geçmişini ekle (Son 4 mesaj - Sohbet hafızası) */
    int hist_start = s_msg_count > 5 ? s_msg_count - 5 : 0;
    for (int i = hist_start; i < s_msg_count; i++) {
        ai_msg_t *m = &s_msgs[i];
        kstrcat(payload, ",{\"role\":\"");
        kstrcat(payload, m->is_user ? "user" : "assistant");
        kstrcat(payload, "\",\"content\":\"");
        append_escaped_json(payload, sizeof(payload) - 64, m->text);
        kstrcat(payload, "\"}");
    }
    kstrcat(payload, "]}");

    char len_str[16];
    kitoa((int)kstrlen(payload), len_str);

    /* HTTP POST İsteği */
    kstrcpy(req, "POST /api/v1/chat/completions HTTP/1.1\r\n");
    kstrcat(req, "Host: openrouter.ai\r\n");
    kstrcat(req, "Authorization: Bearer ");
    kstrcat(req, OPENROUTER_API_KEY);
    kstrcat(req, "\r\n");
    kstrcat(req, "Content-Type: application/json\r\n");
    kstrcat(req, "User-Agent: AxisOS/6.0\r\n");
    kstrcat(req, "Connection: close\r\n");
    kstrcat(req, "Content-Length: ");
    kstrcat(req, len_str);
    kstrcat(req, "\r\n\r\n");
    kstrcat(req, payload);

    return net_web_start("openrouter.ai", 443, req, 1);
}

void ai_send_query(const char *query) {
    if (!query || !query[0]) return;
    add_message("Ali", query, 1);
    s_busy = 1;
    s_busy_started = get_uptime_ms();

    /* 1. Hızlı buton kısayolları için anında yerel sistem eylemleri */
    if (!kstrcmp(query, "Dosyalar")) {
        execute_action("OPEN_FILES");
        add_message("Axis AI", "Dosya Gezgini açıldı. /home ve disklerinizi yönetebilirsiniz.", 0);
        s_busy = 0;
        return;
    }
    if (!kstrcmp(query, "Ayarlar")) {
        execute_action("OPEN_SETTINGS");
        add_message("Axis AI", "Sistem ve Donanım Ayarları açıldı.", 0);
        s_busy = 0;
        return;
    }
    if (!kstrcmp(query, "Tarayıcı")) {
        execute_action("OPEN_BROWSER");
        add_message("Axis AI", "Jade HTML5 Web Tarayıcısı başlatıldı.", 0);
        s_busy = 0;
        return;
    }
    if (!kstrcmp(query, "Terminal")) {
        execute_action("OPEN_TERMINAL");
        add_message("Axis AI", "x86_64 Long Mode Terminal kabuğu açıldı.", 0);
        s_busy = 0;
        return;
    }

    /* 2. İnternet aktifse OpenRouter ile çok turlu gerçek zamanlı sohbet */
    if (net_info.present && net_info.enabled && net_info.link) {
        if (start_openrouter_request()) {
            s_querying_net = 1;
            return;
        }
    }

    /* 3. Ağ yoksa yerel komut sözlüğü ile yanıt ver */
    char local_resp[256];
    if (handle_local_query(query, local_resp, sizeof(local_resp))) {
        add_message("Axis AI", local_resp, 0);
        s_busy = 0;
        return;
    }

    /* 4. Ağ çevrimdışı ve yerel komut eşleşmediyse */
    add_message("Axis AI",
        "Ağ bağlantısı henüz etkin değil. 'Ağ Durumu' penceresinden bağlanabilir ya da 'dosyalar', 'ayarlar', 'terminal', 'yeniden başlat' gibi komutları kullanabilirsiniz.",
        0);
    s_busy = 0;
}

/* JSON içinden "content" alanının başlangıcını bulma */
static const char *find_json_content(const char *data) {
    if (!data) return NULL;
    const char *p = data;
    while ((p = str_find(p, "\"content\"")) != NULL) {
        p += 9;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == ':') {
            p++;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == '"') {
                return p + 1;
            }
        }
    }
    return NULL;
}

/* JSON tırnak içi metni çözme */
static void decode_json_string(const char *src, char *dst, unsigned max_len) {
    if (!src || !dst || !max_len) return;
    unsigned di = 0;
    while (*src && *src != '"' && di < max_len - 1) {
        if (*src == '\\') {
            src++;
            if (!*src) break;
            if (*src == 'n') {
                dst[di++] = ' ';
            } else if (*src == 'r') {
                /* skip */
            } else if (*src == 't') {
                dst[di++] = ' ';
            } else if (*src == 'u' && src[1] && src[2] && src[3] && src[4]) {
                src += 4;
            } else {
                dst[di++] = *src;
            }
            src++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = 0;
}

void ai_poll(uint64_t now) {
    if (!s_querying_net) return;

    int st = net_http_state();
    if (st == NET_DONE) {
        s_querying_net = 0;
        s_busy = 0;
        const char *data = net_http_data();
        if (data) {
            const char *pos = find_json_content(data);
            if (pos) {
                char reply[480];
                decode_json_string(pos, reply, sizeof(reply));

                int had_action = 0;
                if (str_find(reply, "[ACTION:OPEN_FILES]")) {
                    execute_action("OPEN_FILES");
                    strip_action(reply, "[ACTION:OPEN_FILES]");
                    had_action = 1;
                }
                if (str_find(reply, "[ACTION:OPEN_SETTINGS]")) {
                    execute_action("OPEN_SETTINGS");
                    strip_action(reply, "[ACTION:OPEN_SETTINGS]");
                    had_action = 2;
                }
                if (str_find(reply, "[ACTION:OPEN_TERMINAL]")) {
                    execute_action("OPEN_TERMINAL");
                    strip_action(reply, "[ACTION:OPEN_TERMINAL]");
                    had_action = 3;
                }
                if (str_find(reply, "[ACTION:OPEN_BROWSER]")) {
                    execute_action("OPEN_BROWSER");
                    strip_action(reply, "[ACTION:OPEN_BROWSER]");
                    had_action = 4;
                }
                if (str_find(reply, "[ACTION:OPEN_MEDIA]")) {
                    execute_action("OPEN_MEDIA");
                    strip_action(reply, "[ACTION:OPEN_MEDIA]");
                    had_action = 5;
                }
                if (str_find(reply, "[ACTION:REBOOT]")) {
                    execute_action("REBOOT");
                    strip_action(reply, "[ACTION:REBOOT]");
                    had_action = 6;
                }

                if (reply[0] == 0) {
                    if (had_action == 1) str_copy_n(reply, "Dosya Yöneticisi açıldı.", sizeof(reply));
                    else if (had_action == 2) str_copy_n(reply, "Sistem ve Donanım Ayarları açıldı.", sizeof(reply));
                    else if (had_action == 3) str_copy_n(reply, "Terminal konsolu açıldı.", sizeof(reply));
                    else if (had_action == 4) str_copy_n(reply, "Web Tarayıcısı açıldı.", sizeof(reply));
                    else if (had_action == 5) str_copy_n(reply, "Medya oynatıcı açıldı.", sizeof(reply));
                    else if (had_action == 6) str_copy_n(reply, "Sistem yeniden başlatılıyor...", sizeof(reply));
                    else str_copy_n(reply, "İşlem tamamlandı.", sizeof(reply));
                }

                add_message("Axis AI", reply, 0);
            } else {
                const char *err_pos = str_find(data, "\"message\"");
                if (err_pos) {
                    err_pos += 9;
                    while (*err_pos == ' ' || *err_pos == ':' || *err_pos == '\t') err_pos++;
                    if (*err_pos == '"') {
                        char err_txt[160];
                        decode_json_string(err_pos + 1, err_txt, sizeof(err_txt));
                        add_message("Axis AI", err_txt, 0);
                        return;
                    }
                }
                add_message("Axis AI", "İsteğiniz alındı fakat içerik ayrıştırılamadı. Lütfen tekrar deneyin.", 0);
            }
        }
    } else if (st == NET_ERROR) {
        s_querying_net = 0;
        s_busy = 0;
        add_message("Axis AI",
            "Ağ üzerinden OpenRouter sunucusuna ulaşılamadı. Lütfen ağ ayarlarınızı kontrol edin.",
            0);
    } else if (now - s_busy_started > 18000) {
        s_querying_net = 0;
        s_busy = 0;
        add_message("Axis AI", "İstek zaman aşımına uğradı. Lütfen ağ bağlantınızı kontrol edin.", 0);
    }
}

void ai_key(char c) {
    if (!ai_open) return;
    if (c == '\n') {
        if (ai_input_len > 0) {
            ai_send_query(ai_input);
            ai_input_len = 0;
            ai_input[0] = 0;
        }
    } else if (c == '\b') {
        if (ai_input_len > 0) {
            ai_input[--ai_input_len] = 0;
        }
    } else if (c == KBD_ESCAPE) {
        ai_close_drawer();
    } else if ((uint8_t)c >= 32 && c != 127 && ai_input_len < AI_INPUT_MAX - 1) {
        ai_input[ai_input_len++] = c;
        ai_input[ai_input_len] = 0;
    }
}

/* Offcanvas Hit-Test: 700..720 (520px genişlik için uyarlandı) */
int ai_hit_test(int x, int y) {
    if (!ai_open && !ai_anim) return -1;
    if (!ai_surface) return -1;
    int lx = x - ai_surface->x;
    int ly = y - ai_surface->y;
    int sh = (int)ai_surface->h;
    if (lx < 0 || lx >= AI_DRAWER_WIDTH || ly < 0 || ly >= sh) return -1;

    /* Kapat [ × ] Butonu (Üst Sağ) */
    if (lx >= AI_DRAWER_WIDTH - 46 && lx < AI_DRAWER_WIDTH - 10 && ly >= 10 && ly < 46) {
        return 700;
    }
    /* Gönder Butonu (Alt Sağ) */
    if (lx >= 380 && lx < 496 && ly >= sh - 68 && ly < sh - 18) {
        return 701;
    }
    /* Input alanı (Alt Sol) */
    if (lx >= 24 && lx < 370 && ly >= sh - 68 && ly < sh - 18) {
        return 702;
    }
    return 703; /* Drawer gövdesi */
}

void ai_click(int hit, int x, int y) {
    (void)x; (void)y;
    if (hit == 700) {
        ai_close_drawer();
    } else if (hit == 701) {
        if (ai_input_len > 0) {
            ai_send_query(ai_input);
            ai_input_len = 0;
            ai_input[0] = 0;
        }
    } else if (hit == 702) {
        ai_input_focus = 1;
    }
}

/* Akıllı Kelime Kaydırma Fonksiyonu */
static int ai_wrap_text(const char *text, int max_w, char lines[16][128], int max_lines) {
    if (!text || max_w <= 0 || max_lines <= 0) return 0;
    int line_cnt = 0;
    char cur_line[128];
    int cur_len = 0;
    cur_line[0] = 0;

    const char *p = text;
    while (*p && line_cnt < max_lines) {
        if (cur_len == 0 && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
            continue;
        }
        if (*p == '\n') {
            lines[line_cnt][0] = 0;
            str_copy_n(lines[line_cnt++], cur_line, 128);
            cur_line[0] = 0;
            cur_len = 0;
            p++;
            continue;
        }

        char word[128];
        int wlen = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && wlen < 120) {
            word[wlen++] = *p++;
        }
        word[wlen] = 0;

        char test_line[128];
        test_line[0] = 0;
        if (cur_len > 0) {
            str_copy_n(test_line, cur_line, 128);
            int tl = kstrlen(test_line);
            if (tl < 126) {
                test_line[tl++] = ' ';
                test_line[tl] = 0;
            }
            str_copy_n(test_line + tl, word, 128 - tl);
        } else {
            str_copy_n(test_line, word, 128);
        }

        if (ui_text_measure(test_line, 0) <= max_w) {
            str_copy_n(cur_line, test_line, 128);
            cur_len = kstrlen(cur_line);
        } else {
            if (cur_len > 0) {
                str_copy_n(lines[line_cnt++], cur_line, 128);
                if (line_cnt >= max_lines) break;
                str_copy_n(cur_line, word, 128);
                cur_len = kstrlen(cur_line);
            } else {
                str_copy_n(lines[line_cnt++], word, 128);
                cur_line[0] = 0;
                cur_len = 0;
            }
        }
    }

    if (cur_len > 0 && line_cnt < max_lines) {
        str_copy_n(lines[line_cnt++], cur_line, 128);
    }
    return line_cnt;
}

/* Offcanvas Drawer Çizimi: 520px Genişlik, Modern & Ferah Düzen */
void ai_draw(gpu_surface_t *s, uint64_t now) {
    if (!s || !s->pixels) return;
    int w = AI_DRAWER_WIDTH;
    int h = s->h;

    /* Arka plan: Modern saf minimalist beyaz */
    gpu_fill(s, 0xffffffffu);

    /* Sol kenar 1px minimalist sınır */
    gpu_fill_rect(s, 0, 0, 1, h, 0xffe2e8f0u);

    /* Sol kenar yumuşak akrilik gölge (akıcı derinlik hissi) */
    for (int i = 0; i < 8; i++) {
        uint32_t a = (8 - i) * 3;
        gpu_fill_rect(s, 1 + i, 0, 1, h, (a << 24) | 0x000F172Au);
    }

    /* Üst Alan: Modern minimalist başlık */
    ui_text(s, 24, 18, "Axis AI Asistanı", 0xff64748bu, 0);

    /* Kapat Butonu [ × ] (Üst Sağ) */
    ui_round(s, w - 42, 14, 28, 28, 8, 0xfff8fafcu);
    gpu_draw_rect(s, w - 42, 14, 28, 28, 0xffe2e8f0u, 1);
    gpu_draw_line(s, w - 33, 23, w - 23, 33, 0xff64748bu);
    gpu_draw_line(s, w - 23, 23, w - 33, 33, 0xff64748bu);

    /* Alan Sınırları - Ferah Dikey Alan (Gereksiz çipler kaldırılarak alttan +50px kazanıldı) */
    int top_y = 54;
    int input_y = h - 68;
    int bottom_limit = input_y - 18;
    int avail_h = bottom_limit - top_y;

    /* Dinamik Yükseklik ve Satır Hesaplama (Ferah Satır Aralığı: 26px) */
    int m_heights[AI_MAX_MSGS];
    char m_lines[AI_MAX_MSGS][16][128];
    int m_line_cnt[AI_MAX_MSGS];
    int m_widths[AI_MAX_MSGS];

    for (int i = 0; i < s_msg_count; i++) {
        ai_msg_t *m = &s_msgs[i];
        int max_w = m->is_user ? 330 : 380;
        m_line_cnt[i] = ai_wrap_text(m->text, max_w, m_lines[i], 16);
        if (m_line_cnt[i] <= 0) m_line_cnt[i] = 1;

        /* En geniş satırı bularak balon genişliğini dinamik hesapla */
        int max_lw = 0;
        for (int l = 0; l < m_line_cnt[i]; l++) {
            int lw = ui_text_measure(m_lines[i][l], 0);
            if (lw > max_lw) max_lw = lw;
        }

        if (m->is_user) {
            int bw = max_lw + 38;
            if (bw < 72) bw = 72;
            if (bw > 360) bw = 360;
            m_widths[i] = bw;
            m_heights[i] = 28 + m_line_cnt[i] * 26;
        } else {
            int bw = max_lw + 42;
            if (bw < 140) bw = 140;
            if (bw > 430) bw = 430;
            m_widths[i] = bw;
            m_heights[i] = 28 + m_line_cnt[i] * 26;
        }
    }

    int busy_h = s_busy ? 52 : 0;
    int needed_h = busy_h;
    for (int i = 0; i < s_msg_count; i++) needed_h += m_heights[i] + 18;

    int start_i = 0;
    if (needed_h > avail_h) {
        int accum = busy_h;
        start_i = s_msg_count - 1;
        while (start_i > 0) {
            if (accum + m_heights[start_i] + 18 > avail_h) break;
            accum += m_heights[start_i] + 18;
            start_i--;
        }
    }

    /* Mesaj Balonlarını Çizme: Rahat, Sıkışmayan, Simetrik İç Boşluklu */
    int cur_y = top_y;
    for (int i = start_i; i < s_msg_count; i++) {
        ai_msg_t *m = &s_msgs[i];
        int bh = m_heights[i];
        int bw = m_widths[i];
        if (cur_y + bh > bottom_limit) break;

        if (m->is_user) {
            /* Kullanıcı Balonu: Sağa yaslı, açık slate yumuşak kart */
            int bx = w - bw - 24;

            /* Balon hafif zemin gölgesi */
            gpu_fill_rect(s, bx + 2, cur_y + bh, bw - 4, 2, 0x0a0f172au);

            ui_round(s, bx, cur_y, bw, bh, 16, 0xfff1f5f9u);
            gpu_draw_rect(s, bx, cur_y, bw, bh, 0xffe2e8f0u, 1);
            for (int l = 0; l < m_line_cnt[i]; l++) {
                ui_text(s, bx + 18, cur_y + 14 + l * 26, m_lines[i][l], 0xff0f172au, 0);
            }
            cur_y += bh + 18;
        } else {
            /* Asistan Balonu: Sola yaslı, saf beyaz ferah kart */
            int bx = 24;

            /* Balon hafif zemin gölgesi */
            gpu_fill_rect(s, bx + 2, cur_y + bh, bw - 4, 2, 0x0a0f172au);

            ui_round(s, bx, cur_y, bw, bh, 16, 0xffffffffu);
            gpu_draw_rect(s, bx, cur_y, bw, bh, 0xffe2e8f0u, 1);
            for (int l = 0; l < m_line_cnt[i]; l++) {
                ui_text(s, bx + 20, cur_y + 14 + l * 26, m_lines[i][l], 0xff1e293bu, 0);
            }
            cur_y += bh + 18;
        }
    }

    /* "Düşünüyor..." Göstergesi */
    if (s_busy && cur_y + 40 <= bottom_limit) {
        ui_round(s, 24, cur_y, 170, 38, 12, 0xfff8fafcu);
        gpu_draw_rect(s, 24, cur_y, 170, 38, 0xffe2e8f0u, 1);
        int dot_step = (int)((now / 300) % 4);
        char dots[24] = "Düşünüyor";
        for (int d = 0; d < dot_step; d++) kstrcat(dots, ".");
        ui_text(s, 36, cur_y + 11, dots, 0xff64748bu, 0);
    }

    /* ALT GİRİŞ VE GÖNDER ÇUBUĞU */
    gpu_fill_rect(s, 0, input_y - 14, w, 1, 0xffe2e8f0u);

    int inp_w = 344;
    ui_round(s, 24, input_y, inp_w, 48, 14, 0xffffffffu);
    gpu_draw_rect(s, 24, input_y, inp_w, 48, ai_input_focus ? 0xff0f172au : 0xffe2e8f0u, 1);

    if (ai_input_len > 0) {
        ui_text_fit(s, 38, input_y + 15, inp_w - 48, ai_input, 0xff0f172au, 0);
        if ((now / 500) % 2 == 0) {
            int cx = 38 + ui_text_measure(ai_input, 0);
            if (cx < 24 + inp_w - 14) gpu_fill_rect(s, cx, input_y + 14, 2, 20, 0xff0f172au);
        }
    } else {
        ui_text_fit(s, 38, input_y + 15, inp_w - 48, "Bir şeyler yazın veya soru sorun...", 0xff94a3b8u, 0);
    }

    /* Gönder Butonu - Modern koyu slate minimalist */
    int btn_x = 380;
    int btn_w = 96;
    ui_round(s, btn_x, input_y, btn_w, 48, 14, 0xff0f172au);
    ui_text(s, btn_x + 24, input_y + 15, "Gönder", 0xffffffffu, 0);
}
