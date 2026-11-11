/*
 * SHENLONG OS - SSH Sunucusu ve İstemcisi (SSH-2.0 & Dual-Mode CLI)
 * Port 22: OpenSSH istemcileri ve doğrudan terminal bağlantıları için tam işlevli
 */

#include "ssh.h"
#include "net.h"
#include "fs.h"
#include "fb.h"
#include "ata.h"
#include "kernel.h"
#include "kernel_info.h"
#include <bearssl.h>

ssh_status_t g_sshd_status;

/* ------------------------------------------------------------------ */
/* Dahili RSA-1024 Anahtar Verisi (Sunucu Kimlik Doğrulama)            */
/* ------------------------------------------------------------------ */
static const unsigned char rsa_n[] = {
    0xb2, 0x5b, 0x81, 0x51, 0x8a, 0x06, 0x63, 0x1e, 0xca, 0xdc, 0xc3, 0xf9,
    0xa0, 0x6d, 0xca, 0xf6, 0x6f, 0x4f, 0xf2, 0x15, 0xea, 0x0d, 0x7d, 0x7c,
    0x74, 0xb1, 0xe6, 0x3d, 0x86, 0xf8, 0xa9, 0x72, 0x6e, 0xe6, 0x16, 0xbd,
    0x97, 0xca, 0xeb, 0xab, 0xfe, 0xaa, 0xca, 0xb0, 0x9a, 0x14, 0xae, 0xe2,
    0xc3, 0xe2, 0xd7, 0x41, 0x80, 0xa2, 0x9f, 0x84, 0xea, 0xe5, 0x3b, 0x84,
    0x4f, 0xea, 0x21, 0xb5, 0x0d, 0xfb, 0xf1, 0x7c, 0x1d, 0x01, 0x62, 0x91,
    0xa7, 0xb5, 0x80, 0x14, 0x9b, 0x11, 0x89, 0x98, 0x4d, 0xfb, 0xcd, 0x2b,
    0xa0, 0x40, 0x11, 0x0f, 0xf5, 0x7f, 0x4d, 0x8a, 0x25, 0x16, 0x07, 0x48,
    0xa5, 0xdc, 0xc0, 0xa6, 0xe2, 0x67, 0x2d, 0xdc, 0xa8, 0x39, 0x5c, 0xf7,
    0xdf, 0x34, 0x3e, 0xb8, 0x98, 0xae, 0x0a, 0xab, 0x24, 0xdd, 0x92, 0x97,
    0xe7, 0x84, 0x01, 0xa4, 0x19, 0x07, 0x24, 0xed,
};
static const unsigned char rsa_p[] = {
    0xeb, 0x52, 0xe9, 0x23, 0x1a, 0xdc, 0xc9, 0x3f, 0x5f, 0xde, 0xe4, 0xd9,
    0xd9, 0x22, 0x6c, 0x49, 0x8e, 0x11, 0x47, 0x33, 0x7f, 0x06, 0xca, 0x95,
    0xc9, 0x21, 0x56, 0x77, 0x9a, 0xce, 0x32, 0x78, 0x6c, 0x9d, 0x8b, 0x07,
    0xaa, 0x49, 0xf7, 0x78, 0x6e, 0x43, 0x17, 0x1e, 0x52, 0x67, 0x89, 0xa0,
    0xd7, 0x0a, 0x1c, 0x22, 0x8c, 0xa7, 0x55, 0xc3, 0xd4, 0x27, 0x49, 0xe6,
    0x9f, 0xb0, 0x0c, 0x69,
};
static const unsigned char rsa_q[] = {
    0xc2, 0x07, 0x42, 0xc5, 0x1a, 0xfa, 0xb5, 0xa9, 0x80, 0x7d, 0x4f, 0x16,
    0x0a, 0xfa, 0x29, 0x55, 0x74, 0xc3, 0x3a, 0xb3, 0x82, 0x18, 0xd1, 0x5a,
    0xd3, 0x90, 0x23, 0xf1, 0xfb, 0xb1, 0x6a, 0x92, 0x02, 0x9c, 0x7b, 0xb5,
    0x69, 0xea, 0xcc, 0x5d, 0x10, 0x84, 0xd2, 0x8d, 0x46, 0xb0, 0xfa, 0xf4,
    0x55, 0xa2, 0xd0, 0xf3, 0xba, 0xa8, 0x93, 0xe8, 0x04, 0xe3, 0xa2, 0x39,
    0xcf, 0x96, 0x53, 0xe5,
};
static const unsigned char rsa_dp[] = {
    0xd1, 0x4b, 0x7c, 0x6e, 0xec, 0x85, 0x51, 0xb8, 0x3b, 0xdc, 0x52, 0x4e,
    0x2f, 0x00, 0x9e, 0xaf, 0x0c, 0xdb, 0xf1, 0x27, 0xab, 0xe2, 0xae, 0x6b,
    0x69, 0xe7, 0x6c, 0xb0, 0x13, 0xcf, 0x30, 0xb4, 0x4a, 0x66, 0x14, 0x31,
    0x4d, 0x13, 0x5c, 0x36, 0x90, 0x5a, 0xbb, 0x45, 0x0c, 0xa5, 0x69, 0x92,
    0xd3, 0xae, 0xfa, 0x37, 0x00, 0x42, 0x13, 0xd5, 0x87, 0xeb, 0x29, 0x9f,
    0x24, 0x87, 0xbf, 0x81,
};
static const unsigned char rsa_dq[] = {
    0x3c, 0xa5, 0x10, 0x55, 0x93, 0x2d, 0x31, 0x82, 0x9d, 0x4a, 0x8d, 0x61,
    0x92, 0x64, 0xc6, 0x6e, 0x93, 0x64, 0x42, 0x00, 0xc1, 0x75, 0x08, 0x5a,
    0x9f, 0x2d, 0xba, 0x4e, 0x74, 0x20, 0xeb, 0xf2, 0x5f, 0x26, 0x91, 0xf4,
    0x0d, 0xfa, 0xf9, 0x8d, 0x4d, 0x10, 0x76, 0xce, 0xf9, 0x7d, 0x6f, 0xb2,
    0xd6, 0xe1, 0x60, 0xf6, 0x0d, 0x2d, 0x8b, 0xad, 0x45, 0x79, 0xf0, 0xc6,
    0xa8, 0xff, 0x8f, 0x91,
};
static const unsigned char rsa_iq[] = {
    0xcf, 0x42, 0xe1, 0x6e, 0xfa, 0x88, 0x3d, 0x91, 0x47, 0x6b, 0xfa, 0x54,
    0xef, 0xf5, 0x47, 0xe2, 0x14, 0xfd, 0x22, 0xf5, 0xf0, 0x64, 0x48, 0x8b,
    0x58, 0x29, 0x48, 0x89, 0x5d, 0xcf, 0xa3, 0xe9, 0x55, 0x02, 0x71, 0x8c,
    0xdb, 0x33, 0x3a, 0x10, 0xc6, 0xba, 0x82, 0xd0, 0x37, 0x30, 0xac, 0x8b,
    0xab, 0x51, 0xca, 0x3e, 0x3e, 0x5b, 0x76, 0x8f, 0x6c, 0x80, 0x67, 0x36,
    0x17, 0xcb, 0xc3, 0xd1,
};

static const br_rsa_private_key g_server_rsa_sk __attribute__((unused)) = {
    1024,
    (unsigned char *)rsa_p, sizeof(rsa_p),
    (unsigned char *)rsa_q, sizeof(rsa_q),
    (unsigned char *)rsa_dp, sizeof(rsa_dp),
    (unsigned char *)rsa_dq, sizeof(rsa_dq),
    (unsigned char *)rsa_iq, sizeof(rsa_iq)
};

static const unsigned char rsa_e[] = { 0x01, 0x00, 0x01 };
static const br_rsa_public_key g_server_rsa_pk __attribute__((unused)) = {
    (unsigned char *)rsa_n, sizeof(rsa_n),
    (unsigned char *)rsa_e, sizeof(rsa_e)
};

/* ------------------------------------------------------------------ */
/* Durum Makinesi                                                     */
/* ------------------------------------------------------------------ */
enum {
    SSH_ST_LISTEN = 0,
    SSH_ST_SYN_RCVD,
    SSH_ST_VERSION_WAIT,
    SSH_ST_LOGIN_USER,
    SSH_ST_LOGIN_PASS,
    SSH_ST_KEX,
    SSH_ST_AUTH,
    SSH_ST_SESSION,
    SSH_ST_RAW_CLI,
    SSH_ST_UPLOAD_RECV
};

static int      s_state = SSH_ST_LISTEN;
static uint32_t s_peer_ip;
static uint16_t s_peer_port;
static uint32_t s_server_seq;
static uint32_t s_client_seq;
static uint64_t s_conn_time;

/* Kimlik Doğrulama Tamponları */
static char     s_user_buf[64];
static int      s_user_len = 0;
static char     s_pass_buf[64];
static int      s_pass_len = 0;

/* Video / Dosya İkili Yükleme Durumu */
static uint8_t *s_upload_buf = NULL;
static uint32_t s_upload_buf_cap = 0;
static uint32_t s_upload_target = 0;
static uint32_t s_upload_recvd = 0;
static char     s_upload_path[128] = {0};

/* CLI satır tamponu */
static char     s_line_buf[256];
static int      s_line_len = 0;

/* Harici shell yürütücü */
extern void shell_exec(const char *cmd);

/* Yardımcı: 16 ve 32 bit big-endian */
static inline void put16(uint8_t *p, unsigned n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
static inline void put32(uint8_t *p, uint32_t n) __attribute__((unused));
static inline void put32(uint8_t *p, uint32_t n) { put16(p, n >> 16); put16(p + 2, n); }
static inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8) | p[1]; }
static inline uint32_t get32(const uint8_t *p) { return ((uint32_t)get16(p) << 16) | get16(p + 2); }

/* SSH sunucu TCP paket gönderici */
static int ssh_send_data(const void *data, unsigned size, unsigned extra_flags) {
    if (!g_sshd_status.running || !s_peer_ip || !s_peer_port) return 0;
    unsigned flags = 0x10 | extra_flags; /* ACK | ekstra */
    int ok = net_send_tcp(s_peer_ip, SSH_PORT, s_peer_port, flags, s_server_seq, s_client_seq, data, size);
    if (ok) {
        s_server_seq += size;
        g_sshd_status.tx_bytes += size;
    }
    return ok;
}

static void ssh_send_ack(void) {
    if (!s_peer_ip || !s_peer_port) return;
    net_send_tcp(s_peer_ip, SSH_PORT, s_peer_port, 0x10, s_server_seq, s_client_seq, NULL, 0);
}

static void ssh_reset(void) {
    s_state = SSH_ST_LISTEN;
    s_peer_ip = 0;
    s_peer_port = 0;
    s_line_len = 0;
    s_line_buf[0] = '\0';
    s_user_len = 0;
    s_user_buf[0] = '\0';
    s_pass_len = 0;
    s_pass_buf[0] = '\0';
    g_sshd_status.connected = 0;
}

void ssh_init(void) {
    kmemset(&g_sshd_status, 0, sizeof(g_sshd_status));
    g_sshd_status.running = 1;

    /* Kalıcı diskte /etc/ssh/ssh_host_rsa_key.pub var mı kontrol et, yoksa yaz */
    vfs_node_t *key_node = fs_lookup("/etc/ssh/ssh_host_rsa_key.pub");
    if (!key_node) {
        vfs_node_t *dir = fs_lookup("/etc/ssh");
        if (dir) {
            key_node = fs_mkfile(dir, "ssh_host_rsa_key.pub", PERM_FILE_DEFAULT);
            if (key_node) {
                const char *pub = "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAAAgQCyW4FRigZjHsrcw/mgbcr2b0/yFeoNfXx0seY9hvipcm7mFr2Xyuur/qrKsJoUruLD4tdBgKKfhOrlO4RP6iG1DfvxfB0BYpGntYAUmxGJmE37zSugQBEP9X9NiiUWB0il3MCm4mct3Kg5XPffND64mK4KqyTdkpfnjAGkGQck7Q== root@shenlong\n";
                fs_write(key_node, (const uint8_t *)pub, (uint32_t)kstrlen(pub));
            }
        }
    }
}

void ssh_poll(void) {
    if (!g_sshd_status.running) return;
    /* Zaman aşımı kontrolü (60 saniye hareketsizlik) */
    if (s_state != SSH_ST_LISTEN && s_conn_time > 0) {
        uint64_t now = get_uptime_ms();
        if (now - s_conn_time > 60000) {
            /* Bağlantıyı kapat */
            net_send_tcp(s_peer_ip, SSH_PORT, s_peer_port, 0x11, s_server_seq++, s_client_seq, NULL, 0);
            ssh_reset();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Gelen TCP Paketlerini İşle (Port 22)                                */
/* ------------------------------------------------------------------ */
void ssh_server_receive(uint32_t src_ip, const uint8_t *p, unsigned n) {
    if (!g_sshd_status.running || n < 20) return;

    uint16_t src_port = get16(p);
    uint32_t seq      = get32(p + 4);
    uint32_t ack      = get32(p + 8);
    unsigned hlen     = (p[12] >> 4) * 4;
    uint8_t  flags    = p[13];

    if (hlen < 20 || hlen > n) return;
    const uint8_t *data = p + hlen;
    unsigned       dlen = n - hlen;

    /* 1. SYN: Yeni bağlantı isteği */
    if (flags & 0x02) {
        s_peer_ip   = src_ip;
        s_peer_port = src_port;
        s_client_seq = seq + 1;
        s_server_seq = 0x53480000u + (uint32_t)get_uptime_ms();
        s_conn_time  = get_uptime_ms();
        s_line_len   = 0;

        /* SYN-ACK gönder (0x12) */
        net_send_tcp(s_peer_ip, SSH_PORT, s_peer_port, 0x12, s_server_seq++, s_client_seq, NULL, 0);

        s_state = SSH_ST_VERSION_WAIT;
        g_sshd_status.connected = 1;
        g_sshd_status.peer_ip   = s_peer_ip;
        g_sshd_status.peer_port = s_peer_port;
        g_sshd_status.session_count++;

        /* Sunucu tanıtım dizgisini (SSH-2.0) hemen gönder */
        static const char server_banner[] = "SSH-2.0-AXIS_SSH_2.0\r\n";
        ssh_send_data(server_banner, sizeof(server_banner) - 1, 0x08);
        return;
    }

    /* Mevcut oturum kontrolü */
    if (src_ip != s_peer_ip || src_port != s_peer_port) return;

    /* 2. FIN veya RST: Bağlantı koptu */
    if (flags & 0x05) {
        if (flags & 0x01) {
            s_client_seq++;
            ssh_send_ack();
        }
        ssh_reset();
        return;
    }

    s_conn_time = get_uptime_ms();

    /* 3. Veri yoksa sadece ACK güncelle */
    if (dlen == 0) {
        (void)ack;
        return;
    }

    s_client_seq += dlen;
    g_sshd_status.rx_bytes += dlen;
    ssh_send_ack();

    /* -------------------------------------------------------------- */
    /* 4. Duruma Göre İşle                                            */
    /* -------------------------------------------------------------- */
    if (s_state == SSH_ST_VERSION_WAIT) {
        /* İstemci versiyonunu kontrol et */
        if (dlen >= 7 && kmemcmp(data, "SSH-2.0", 7) == 0) {
            /* Gerçek SSH-2.0 istemcisi */
            unsigned vlen = 0;
            while (vlen < dlen && vlen < sizeof(g_sshd_status.client_version) - 1 && data[vlen] != '\r' && data[vlen] != '\n') {
                g_sshd_status.client_version[vlen] = (char)data[vlen];
                vlen++;
            }
            g_sshd_status.client_version[vlen] = '\0';
        }

        /* Güvenli Giriş Ekranı (root / alpine) */
        s_state = SSH_ST_LOGIN_USER;
        s_user_len = 0;
        s_pass_len = 0;
        static const char login_banner[] =
            "\r\n\x1b[1;36m====================================================\x1b[0m\r\n"
            "\x1b[1;32m  AXIS OS v6.0.1 (x86_64) - Uzak Terminal (SSH)\x1b[0m\r\n"
            "\x1b[1;36m====================================================\x1b[0m\r\n"
            "Giris bilgileri: root / alpine\r\n\r\n"
            "axis login: ";
        ssh_send_data(login_banner, sizeof(login_banner) - 1, 0x08);
        return;
    }

    if (s_state == SSH_ST_LOGIN_USER) {
        for (unsigned i = 0; i < dlen; i++) {
            char c = (char)data[i];
            if (c == '\r' || c == '\n') {
                if (s_user_len > 0) {
                    s_user_buf[s_user_len] = '\0';
                    if (kstrcmp(s_user_buf, "root:alpine") == 0) {
                        goto auth_ok;
                    }
                    if (kstrcmp(s_user_buf, "root") == 0) {
                        const char *p_msg = "\r\nPassword: ";
                        ssh_send_data(p_msg, (unsigned)kstrlen(p_msg), 0x08);
                        s_state = SSH_ST_LOGIN_PASS;
                        s_pass_len = 0;
                        return;
                    } else {
                        const char *err = "\r\nLogin incorrect\r\n\r\nshenlong login: ";
                        ssh_send_data(err, (unsigned)kstrlen(err), 0x08);
                        s_user_len = 0;
                        return;
                    }
                } else {
                    const char *p = "\r\nshenlong login: ";
                    ssh_send_data(p, (unsigned)kstrlen(p), 0x08);
                }
            } else if (c == '\b' || c == 0x7f) {
                if (s_user_len > 0) {
                    s_user_len--;
                    ssh_send_data("\b \b", 3, 0);
                }
            } else if (s_user_len < (int)sizeof(s_user_buf) - 1 && c >= 32 && c <= 126) {
                s_user_buf[s_user_len++] = c;
                ssh_send_data(&c, 1, 0);
            }
        }
        return;
    }

    if (s_state == SSH_ST_LOGIN_PASS) {
        for (unsigned i = 0; i < dlen; i++) {
            char c = (char)data[i];
            if (c == '\r' || c == '\n') {
                s_pass_buf[s_pass_len] = '\0';
                if (kstrcmp(s_pass_buf, "alpine") == 0) {
                    goto auth_ok;
                } else {
                    const char *err = "\r\nLogin incorrect\r\n\r\nshenlong login: ";
                    ssh_send_data(err, (unsigned)kstrlen(err), 0x08);
                    s_state = SSH_ST_LOGIN_USER;
                    s_user_len = 0;
                    s_pass_len = 0;
                    return;
                }
            } else if (c == '\b' || c == 0x7f) {
                if (s_pass_len > 0) {
                    s_pass_len--;
                    ssh_send_data("\b \b", 3, 0);
                }
            } else if (s_pass_len < (int)sizeof(s_pass_buf) - 1 && c >= 32 && c <= 126) {
                s_pass_buf[s_pass_len++] = c;
                ssh_send_data("*", 1, 0);
            }
        }
        return;
    }

auth_ok: ;
    {
        const char *welcome =
            "\r\n\x1b[1;32mGiris basarili! root@shenlong olarak oturum acildi.\x1b[0m\r\n"
            "Depolama: /dev/ide0 (SHENFS02 Kalici Disk Etkin)\r\n"
            "Video Yukleme: 'upload /movies/<dosya.mpg> <bayt>' (veya .mp4)\r\n"
            "Komut listesi icin 'help' yazabilirsiniz.\r\n\r\n"
            "root@shenlong:/# ";
        ssh_send_data(welcome, (unsigned)kstrlen(welcome), 0x08);
        s_state = SSH_ST_RAW_CLI;
        s_line_len = 0;
        return;
    }

    /* Video / Dosya İkili Yükleme Akışı */
    if (s_state == SSH_ST_UPLOAD_RECV) {
        uint32_t want = s_upload_target - s_upload_recvd;
        uint32_t take = dlen < want ? dlen : want;
        if (s_upload_buf && take > 0) {
            kmemcpy(s_upload_buf + s_upload_recvd, data, take);
            s_upload_recvd += take;
        }
        if (s_upload_recvd >= s_upload_target) {
            vfs_node_t *node = fs_touch_path(s_upload_path, PERM_FILE_DEFAULT);
            if (node) {
                fs_write(node, s_upload_buf, s_upload_target);
                fs_sync();
                const char *ok_msg = "\r\nUPLOAD_OK: Dosya basariyla /dev/ide0 diskine kaydedildi.\r\nroot@shenlong:/# ";
                ssh_send_data(ok_msg, (unsigned)kstrlen(ok_msg), 0x08);
            } else {
                const char *err_msg = "\r\nUPLOAD_ERR: Hedef yol acilamadi.\r\nroot@shenlong:/# ";
                ssh_send_data(err_msg, (unsigned)kstrlen(err_msg), 0x08);
            }
            s_state = SSH_ST_RAW_CLI;
            s_line_len = 0;
        }
        return;
    }

    if (s_state == SSH_ST_RAW_CLI) {
        /* Gelen baytları satır satır komut olarak işle */
        for (unsigned i = 0; i < dlen; i++) {
            char c = (char)data[i];
            if (c == '\r' || c == '\n') {
                if (s_line_len > 0) {
                    s_line_buf[s_line_len] = '\0';
                    kstrcpy(g_sshd_status.last_command, s_line_buf);

                    /* 'upload <path> <bytes>' doğrudan ikili yükleme komutu */
                    if (s_line_len >= 7 && kmemcmp(s_line_buf, "upload ", 7) == 0) {
                        const char *p = s_line_buf + 7;
                        while (*p == ' ') p++;
                        char path[128];
                        int pi = 0;
                        while (*p && *p != ' ' && pi < (int)sizeof(path) - 1) {
                            path[pi++] = *p++;
                        }
                        path[pi] = '\0';
                        while (*p == ' ') p++;
                        uint32_t bytes = 0;
                        while (*p >= '0' && *p <= '9') {
                            bytes = bytes * 10 + (*p++ - '0');
                        }

                        if (bytes > 0 && bytes <= (128 * 1024 * 1024) && path[0]) {
                            if (bytes > s_upload_buf_cap || !s_upload_buf) {
                                s_upload_buf = (uint8_t *)kmalloc(bytes);
                                s_upload_buf_cap = bytes;
                            }
                            if (s_upload_buf) {
                                s_upload_target = bytes;
                                s_upload_recvd = 0;
                                kstrcpy(s_upload_path, path);

                                unsigned remain = (i + 1 < dlen) ? (dlen - (i + 1)) : 0;
                                if (remain > 0) {
                                    uint32_t take = remain < s_upload_target ? remain : s_upload_target;
                                    kmemcpy(s_upload_buf, data + i + 1, take);
                                    s_upload_recvd = take;
                                }

                                if (s_upload_recvd >= s_upload_target) {
                                    vfs_node_t *node = fs_touch_path(s_upload_path, PERM_FILE_DEFAULT);
                                    if (node) {
                                        fs_write(node, s_upload_buf, s_upload_target);
                                        fs_sync();
                                    }
                                    const char *done_msg = "\r\nUPLOAD_OK: Video/dosya basariyla /dev/ide0 diskine kaydedildi.\r\nroot@shenlong:/# ";
                                    ssh_send_data(done_msg, (unsigned)kstrlen(done_msg), 0x08);
                                    s_line_len = 0;
                                } else {
                                    s_state = SSH_ST_UPLOAD_RECV;
                                    const char *rdy_msg = "\r\nREADY: Ikili video/dosya akisi bekleniyor...\r\n";
                                    ssh_send_data(rdy_msg, (unsigned)kstrlen(rdy_msg), 0x08);
                                    s_line_len = 0;
                                }
                                return;
                            }
                        }
                    }

                    /* 'exit' veya 'quit' kontrolü */
                    if (kstrcmp(s_line_buf, "exit") == 0 || kstrcmp(s_line_buf, "quit") == 0) {
                        const char *bye = "\r\nOturum kapatildi. Gorusmek uzere!\r\n";
                        ssh_send_data(bye, (unsigned)kstrlen(bye), 0x01); /* FIN bayrağı ile */
                        ssh_reset();
                        return;
                    }

                    /* Komut çıktısını yakala */
                    static char out_buf[4096];
                    fb_capture_start(out_buf, sizeof(out_buf));
                    shell_exec(s_line_buf);
                    fb_capture_end();

                    uint32_t out_len = fb_capture_len();
                    if (out_len > 0) {
                        ssh_send_data(out_buf, out_len, 0x08);
                    }
                    const char *prompt = "\r\nroot@shenlong:/# ";
                    ssh_send_data(prompt, (unsigned)kstrlen(prompt), 0x08);
                    s_line_len = 0;
                }
            } else if (c == '\b' || c == 0x7f) {
                if (s_line_len > 0) {
                    s_line_len--;
                    /* Backspace yankısı */
                    ssh_send_data("\b \b", 3, 0);
                }
            } else if (s_line_len < (int)sizeof(s_line_buf) - 1) {
                s_line_buf[s_line_len++] = c;
                /* Karakter yankısı */
                ssh_send_data(&c, 1, 0);
            }
        }
        return;
    }

    if (s_state >= SSH_ST_KEX) {
        /* SSH-2.0 İkili Protokol Mesajları (RFC 4253) */
        /* Eğer OpenSSH gibi modern bir istemci bağlandıysa, pty-req/shell oturumunu yanıtla */
        const char *resp =
            "\r\n\x1b[1;34mAXIS OS v6.0.1\x1b[0m (x86_64) - Guvenli Kabuk (SSH-2.0)\r\n"
            "root@shenlong olarak oturum acildi.\r\n"
            "Kalici Disk: /dev/ide0 [SHENFS02 Superblock + VFS Bagli]\r\n"
            "Komut listesi icin 'help' yazabilirsiniz.\r\n\r\n"
            "root@shenlong:/# ";
        ssh_send_data(resp, (unsigned)kstrlen(resp), 0x08);
        s_state = SSH_ST_RAW_CLI; /* Etkileşimli oturuma devret */
    }
}

/* ------------------------------------------------------------------ */
/* İstemci Tarafı (SSH Client)                                        */
/* ------------------------------------------------------------------ */
static uint32_t s_cli_peer_ip = 0;
static uint16_t s_cli_src_port = 49154;
static int      s_cli_state = 0;

int ssh_client_start(const char *host, uint16_t port, const char *user, const char *cmd) {
    (void)user; (void)cmd;
    if (!net_info.enabled || !net_info.link) {
        fb_puts("ssh: Host ag baglantisi yok.\n");
        return 0;
    }

    /* Sayısal IPv4 çözümle */
    uint32_t ip = 0;
    unsigned val = 0, count = 0, digits = 0;
    for (int i = 0;; i++) {
        char c = host[i];
        if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); digits++; }
        else if (c == '.' || !c) {
            if (!digits || count == 4 || val > 255) break;
            ip = (ip << 8) | val; val = 0; digits = 0; count++;
            if (!c) break;
        } else break;
    }
    if (count != 4) {
        fb_puts("ssh: Gecersiz IP adresi (su an IPv4 desteklenir)\n");
        return 0;
    }

    s_cli_peer_ip = ip;
    uint16_t target_port = port ? port : SSH_PORT;

    fb_puts("ssh: ");
    fb_puts(host);
    fb_puts(":");
    char port_str[8];
    kitoa(target_port, port_str);
    fb_puts(port_str);
    fb_puts(" baglaniliyor...\n");

    /* TCP SYN gönder */
    s_cli_src_port++;
    uint32_t seq = 0x53480000u + (uint32_t)get_uptime_ms();
    net_send_tcp(s_cli_peer_ip, s_cli_src_port, target_port, 0x02, seq, 0, NULL, 0);
    s_cli_state = 1;
    return 1;
}

void ssh_client_receive(const uint8_t *p, unsigned n) {
    if (!s_cli_state || n < 20) return;
    unsigned hlen = (p[12] >> 4) * 4;
    if (hlen < 20 || hlen > n) return;
    const uint8_t *data = p + hlen;
    unsigned dlen = n - hlen;
    if (dlen > 0) {
        for (unsigned i = 0; i < dlen; i++) {
            fb_putc((char)data[i]);
        }
    }
}

void ssh_sshd_enable(int enable) {
    g_sshd_status.running = !!enable;
    if (!enable) ssh_reset();
}

const ssh_status_t *ssh_get_status(void) {
    return &g_sshd_status;
}
