/*
 * SHENLONG OS - Shell v0.2
 * Gerçek sistem verileri, AAA kalite komut çıktıları
 */

#include "../kernel/kernel.h"
#include "../kernel/fb.h"
#include "../kernel/keyboard.h"
#include "../kernel/fs.h"
#include "../kernel/kernel_info.h"
#include "../kernel/ui.h"
#include "../kernel/clock.h"
#include "../kernel/net.h"
#include "../kernel/browser.h"
#include "../kernel/media.h"
#include "../kernel/audio.h"
#include "../kernel/ata.h"
#include "../kernel/ssh.h"

/* unused variable uyarısını bastır */
#define _unused(x) ((void)(x))

#define CMD_BUF_SIZE  256
#define MAX_ARGS      16
#define HOSTNAME      "axis"
#define OS_NAME       "AXIS OS"
#define OS_VERSION    "6.0.2"
#define KERNEL_VER    "axis-6.0.2-x86_64"

/* ------------------------------------------------------------------ */
/* History                                                            */
/* ------------------------------------------------------------------ */
#define HISTORY_SIZE 32
static char history[HISTORY_SIZE][CMD_BUF_SIZE];
static int  hist_count = 0;
static int  hist_idx   = 0;

static void hist_add(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    if (hist_count > 0 && kstrcmp(history[(hist_count-1)%HISTORY_SIZE], cmd)==0) return;
    kstrcpy(history[hist_count % HISTORY_SIZE], cmd);
    hist_count++;
    hist_idx = hist_count;
}

/* ------------------------------------------------------------------ */
/* Argüman ayrıştırıcı                                               */
/* ------------------------------------------------------------------ */
static int parse_args(char *line, char **argv) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p==' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p!=' ') p++;
        if (*p) *p++='\0';
        if (argc >= MAX_ARGS-1) break;
    }
    argv[argc] = NULL;
    return argc;
}

/* ------------------------------------------------------------------ */
/* CWD                                                                */
/* ------------------------------------------------------------------ */
static vfs_node_t *cwd_node = NULL;

static void get_cwd(char *out, int max) {
    if (!cwd_node || cwd_node == fs_root) { out[0]='/'; out[1]='\0'; return; }
    char tmp[256]; out[0]='\0';
    vfs_node_t *n = cwd_node;
    while (n && n != fs_root) {
        kstrcpy(tmp,"/"); kstrcat(tmp,n->name); kstrcat(tmp,out);
        kstrcpy(out,tmp); n=n->parent;
        if ((int)kstrlen(out)>=max-1) break;
    }
    if (!out[0]) { out[0]='/'; out[1]='\0'; }
}

/* ------------------------------------------------------------------ */
/* Yardımcı: iki haneli sayı yaz                                     */
/* ------------------------------------------------------------------ */
static void put2(uint8_t v) {
    char s[3]; s[0]='0'+(v/10); s[1]='0'+(v%10); s[2]='\0';
    fb_puts(s);
}

static void put4(uint16_t v) {
    char s[5];
    s[0]='0'+((v/1000)%10); s[1]='0'+((v/100)%10);
    s[2]='0'+((v/10)%10);   s[3]='0'+(v%10); s[4]='\0';
    fb_puts(s);
}

/* ------------------------------------------------------------------ */
/* Alias tablosu                                                      */
/* ------------------------------------------------------------------ */
#define ALIAS_MAX 16
static char alias_name[ALIAS_MAX][32];
static char alias_val [ALIAS_MAX][CMD_BUF_SIZE];
static int  alias_count = 0;

/* ------------------------------------------------------------------ */
/* Yardımcı string fonksiyonları                                      */
/* ------------------------------------------------------------------ */
static const char *kstrstr(const char *hay, const char *needle) {
    if (!needle || !needle[0]) return hay;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return hay;
    }
    return NULL;
}

/* Basit glob: sadece * joker karakteri */
static int glob_match(const char *pat, const char *str) {
    while (*pat && *str) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1;
            while (*str) { if (glob_match(pat, str)) return 1; str++; }
            return 0;
        }
        if (*pat != *str) return 0;
        pat++; str++;
    }
    while (*pat == '*') pat++;
    return !*pat && !*str;
}

static void put_hex2(uint8_t v) {
    char s[3];
    s[0] = "0123456789abcdef"[v >> 4];
    s[1] = "0123456789abcdef"[v & 0xf];
    s[2] = '\0';
    fb_puts(s);
}

/* ------------------------------------------------------------------ */
/* Komutlar                                                           */
/* ------------------------------------------------------------------ */

static void cmd_help(void) {
    fb_puts("SHENLONG OS v"); fb_puts(OS_VERSION); fb_puts(" - Komutlar\n");
    fb_puts("------------------------------------------------\n");
    fb_puts("Dosya sistemi:\n");
    fb_puts("  ls [-l] [-a] [yol]        Dizin listele\n");
    fb_puts("  cat <dosya> [...]          Dosya(lar) goster\n");
    fb_puts("  mkdir <ad>                 Dizin olustur\n");
    fb_puts("  touch <ad>                 Bos dosya olustur\n");
    fb_puts("  rm [-r] <ad>               Sil (dizin icin -r)\n");
    fb_puts("  cp <src> <dst>             Kopyala\n");
    fb_puts("  mv <src> <dst>             Tasi/Yeniden adlandir\n");
    fb_puts("  write <f> <icerik>         Dosyaya yaz\n");
    fb_puts("  pwd                        Mevcut dizin\n");
    fb_puts("  cd <yol>                   Dizin degistir\n");
    fb_puts("  stat <dosya>               Dosya bilgisi\n");
    fb_puts("  chmod <oct> <f>            Izin degistir\n");
    fb_puts("  sync                       Kalici diske yaz (SHENFS02)\n");
    fb_puts("  df                         Disk kullanimi\n");
    fb_puts("  find [yol] [-name p] [-type f|d]  Dosya ara\n");
    fb_puts("  grep [-n] [-v] [-i] <kalip> <f>   Icerikte ara\n");
    fb_puts("  wc [-l] [-w] [-c] <dosya>  Satir/kelime/byte say\n");
    fb_puts("  head [-n N] <dosya>        Ilk N satir\n");
    fb_puts("  tail [-n N] <dosya>        Son N satir\n");
    fb_puts("  hexdump <dosya>            Hex dump\n");
    fb_puts("\nSistem:\n");
    fb_puts("  uname [-a]                 Sistem bilgisi\n");
    fb_puts("  uptime                     Calisma suresi\n");
    fb_puts("  perf [reset]               Cizim performansi sayaclari\n");
    fb_puts("  net [on|off]               Host agi ve paket sayaclari\n");
    fb_puts("  ssh <host> [port]          Uzak SSH sunucusuna baglan\n");
    fb_puts("  sshd [status|start|stop]   SSH daemon durumu (Port 22)\n");
    fb_puts("  web <https://adres>        Web penceresinde ac\n");
    fb_puts("  video [dosya|demo|stop]    Video oynat (yerel dosya / Justin / Demo)\n");
  fb_puts("  upload <dosya> <bayt>      SSH ile ikili video/dosya yukleme modu\n");
    fb_puts("  web status                 Sayfa durumu ve metni\n");
    fb_puts("  date                       Tarih/saat\n");
    fb_puts("  free                       Bellek durumu\n");
    fb_puts("  lscpu                      CPU bilgisi\n");
    fb_puts("  lsmem                      Bellek haritasi\n");
    fb_puts("  dmesg                      Kernel mesajlari\n");
    fb_puts("  ps                         Proses listesi\n");
    fb_puts("  id                         Kullanici kimlik bilgisi\n");
    fb_puts("  whoami                     Kullanici adi\n");
    fb_puts("  hostname                   Makine adi\n");
    fb_puts("  tty                        Terminal aygiti\n");
    fb_puts("\nKabuk / Ortam:\n");
    fb_puts("  echo [-n] <metin>          Metni yazdir\n");
    fb_puts("  env                        Ortam degiskenleri\n");
    fb_puts("  export AD=DEGER            Ortam degiskeni tanimla\n");
    fb_puts("  printenv [AD]              Degiskeni yazdir\n");
    fb_puts("  unset <AD>                 Degiskeni sil\n");
    fb_puts("  alias [ad=deger]           Kisayol tanimla/listele\n");
    fb_puts("  unalias <ad>               Kisayol sil\n");
    fb_puts("  seq [ilk [artis]] son      Sayi dizisi\n");
    fb_puts("  yes [metin]                Tekrarla (20 satir)\n");
    fb_puts("  true / false               Cikis kodu\n");
    fb_puts("  tty                        Terminal adi\n");
    fb_puts("  history                    Komut gecmisi\n");
    fb_puts("  clear                      Ekrani temizle\n");
    fb_puts("  reboot                     Yeniden baslatma\n");
    fb_puts("  help                       Bu yardim\n");
    fb_puts("\nOk tuslari: yukari/asagi = gecmis\n");
}

static void cmd_uname(int argc, char **argv) {
    int all = (argc > 1 && kstrcmp(argv[1], "-a") == 0);
    if (all) {
        /* uname -a: sysname nodename release version machine */
        fb_puts(OS_NAME); fb_putc(' ');
        fb_puts(HOSTNAME); fb_putc(' ');
        fb_puts(KERNEL_VER); fb_putc(' ');
        fb_puts("#1 SMP ");
        /* tarih */
        if (g_sysinfo.boot_year) {
            put4(g_sysinfo.boot_year); fb_putc('-');
            put2(g_sysinfo.boot_month); fb_putc('-');
            put2(g_sysinfo.boot_day);
        } else {
            fb_puts("2026-11-06");
        }
        fb_puts(" x86_64 x86_64 x86_64 "); fb_puts(OS_NAME); fb_putc('\n');
    } else {
        fb_puts(OS_NAME); fb_putc('\n');
    }
}

static void cmd_date(void) {
    wall_time_t now;
    if (clock_read(&now, 0)) {
        char date[16], time[16];
        clock_date_text(&now,date); clock_time_text(&now,time);
        fb_puts(date); fb_puts("  "); fb_puts(time);
        fb_puts("  UTC\n");
    } else {
        fb_puts("Tarih bilgisi alinamadi\n");
    }
}

static void cmd_uptime(void) {
    uint64_t ms = get_uptime_ms();
    uint64_t s  = ms / 1000;
    uint64_t m  = s  / 60;
    uint64_t h  = m  / 60;
    s %= 60; m %= 60;
    char buf[21];
    fb_puts("uptime: ");
    kitoa((int64_t)h, buf); fb_puts(buf); fb_puts("h ");
    kitoa((int64_t)m, buf); fb_puts(buf); fb_puts("m ");
    kitoa((int64_t)s, buf); fb_puts(buf); fb_puts("s");
    fb_puts("  |  load avg: 0.00 0.00 0.00\n");
}

static void cmd_perf(int argc, char **argv) {
    if(argc>1&&!kstrcmp(argv[1],"reset")) { gpu_stats_reset(); return; }
    gpu_stats_t s; gpu_stats_read(&s);
    char b[32];
    fb_puts("frames: "); kitoa(s.frames,b); fb_puts(b);
    fb_puts("\npixels: "); kitoa(s.pixels,b); fb_puts(b);
    fb_puts("\navg pixels/frame: "); kitoa(s.frames?s.pixels/s.frames:0,b); fb_puts(b);
    fb_puts("\navg compose us: ");
    uint64_t mhz=g_sysinfo.tsc_freq_hz/1000000;
    kitoa(s.frames&&mhz?s.ticks/s.frames/mhz:0,b); fb_puts(b);
    fb_puts("\nmax compose us: "); kitoa(mhz?s.max_ticks/mhz:0,b); fb_puts(b);
    fb_puts("\nPresent limit: 100 Hz (not a measured display FPS)\n");
}

static void cmd_net(int argc,char **argv) {
    if(argc>1&&!kstrcmp(argv[1],"on")) net_enable(1);
    if(argc>1&&!kstrcmp(argv[1],"off")) net_enable(0);
    char b[32];
    fb_puts("adapter: "); fb_puts(net_info.present?"rtl8139":"absent");
    fb_puts("\nnetwork: "); fb_puts(!net_info.enabled?"disabled":net_info.gateway?"host-ready":"waiting");
    fb_puts("\nIPv4: "); net_ip_text(net_info.ip,b); fb_puts(b);
    fb_puts("\nrouter: "); net_ip_text(net_info.router,b); fb_puts(b);
    fb_puts("\nDNS: "); net_ip_text(net_info.dns,b); fb_puts(b);
    fb_puts("\nRX: "); kitoa(net_info.rx_packets,b); fb_puts(b);
    fb_puts("\nTX: "); kitoa(net_info.tx_packets,b); fb_puts(b);
    fb_puts("\ndropped: "); kitoa(net_info.dropped,b); fb_puts(b); fb_putc('\n');
}
static void cmd_web(int argc,char **argv) {
    if(argc>1&&!kstrcmp(argv[1],"status")) {
        char b[24]; fb_puts("web state: "); kitoa(net_http_state(),b); fb_puts(b);
        fb_puts("\nURL: "); fb_puts(browser_current_url());
        fb_puts("\ntitle: "); fb_puts(browser_title());
        fb_puts("\nstatus: "); fb_puts(browser_status()); fb_putc('\n');
        for(unsigned i=0;i<10;i++) { const char *line=browser_text_line(i); if(*line) { fb_puts(line); fb_putc('\n'); } }
    } else ui_open_browser(argc>1?argv[1]:NULL);
}

static void cmd_video(int argc,char **argv) {
    if(argc>1&&!kstrcmp(argv[1],"status")) {
        char b[32]; fb_puts("video: "); fb_puts(media_status());
        fb_puts("\nvideo frames: "); kitoa(media_video_frames(),b); fb_puts(b);
        fb_puts("\naudio: "); fb_puts(audio_available()?"AC97 stereo":"absent");
        fb_puts("\naudio frames: "); kitoa(audio_frames(),b); fb_puts(b); fb_putc('\n');
    } else if(argc>1&&!kstrcmp(argv[1],"stop")) media_stop();
    else {
        ui_open_media();
        if(argc>1&&!kstrcmp(argv[1],"demo")) media_demo();
        else if(argc>1&&!kstrcmp(argv[1],"bieber")) media_bieber();
        else if(argc>1) {
            vfs_node_t *node = fs_lookup(argv[1]);
            if (node && node->type == NODE_FILE && node->size > 0) {
                if (media_load(fs_node_data(node), node->size, node->name)) {
                    fb_puts("video: Oynatiliyor: ");
                    fb_puts(node->name);
                    fb_putc('\n');
                } else {
                    fb_puts("video: Video cozulemedi (MPEG-1 Video/Audio beklenir)\n");
                }
            } else {
                fb_puts("video: Dosya bulunamadi: ");
                fb_puts(argv[1]);
                fb_putc('\n');
            }
        }
        else media_bieber();
    }
}
static void cmd_free(void) {
    char buf[32];
    fb_puts("               total        used        free\n");
    fb_puts("Mem:     ");
    if (g_sysinfo.total_memory) {
        fmt_size(g_sysinfo.total_memory, buf); fb_puts(buf);
        fb_puts("    ");
        fmt_size(g_sysinfo.total_memory - g_sysinfo.usable_memory, buf); fb_puts(buf);
        fb_puts("    ");
        fmt_size(g_sysinfo.usable_memory, buf); fb_puts(buf);
    } else {
        fb_puts("N/A         N/A         N/A");
    }
    fb_puts("\nSwap:    0 B         0 B         0 B\n");
}

static void cmd_lscpu(void) {
    char buf[64];
    fb_puts("Architecture:    x86_64\n");
    fb_puts("CPU op-mode(s):  32-bit, 64-bit\n");
    fb_puts("Byte Order:      Little Endian\n");

    /* CPU brand (CPUID'den) */
    const char *brand = get_cpu_brand_str();
    if (brand && brand[0]) {
        fb_puts("Model name:      "); fb_puts(brand); fb_putc('\n');
    }

    /* CPU sayısı */
    kitoa((int64_t)g_sysinfo.cpu_count, buf);
    fb_puts("CPU(s):          "); fb_puts(buf); fb_putc('\n');

    /* TSC frekansı */
    if (g_sysinfo.tsc_freq_hz) {
        fmt_freq(g_sysinfo.tsc_freq_hz, buf);
        fb_puts("CPU MHz:         "); fb_puts(buf); fb_putc('\n');
    }

    /* Firmware */
    fb_puts("Firmware:        ");
    if (g_sysinfo.firmware_type == 2)      fb_puts("UEFI 64-bit\n");
    else if (g_sysinfo.firmware_type == 1) fb_puts("UEFI 32-bit\n");
    else                                   fb_puts("BIOS\n");

    /* Paging */
    fb_puts("Paging:          ");
    fb_puts(g_sysinfo.paging_mode == 1 ? "5-level (57-bit VA)\n" : "4-level (48-bit VA)\n");

    /* KASLR */
    if (g_sysinfo.kernel_virt_base) {
        fb_puts("Kernel VA base:  0x");
        /* hex yaz */
        uint64_t addr = g_sysinfo.kernel_virt_base;
        char hex[17];
        hex[16] = '\0';
        for (int i = 15; i >= 0; i--) {
            uint8_t nib = (uint8_t)(addr & 0xF);
            hex[i] = (nib < 10) ? ('0'+nib) : ('a'+nib-10);
            addr >>= 4;
        }
        /* Baştaki sıfırları atla */
        char *h = hex; while (*h=='0' && *(h+1)) h++;
        fb_puts(h); fb_putc('\n');
    }
}

static void cmd_lsmem(void) {
    char buf[32];
    fb_puts("Total memory:   ");
    fmt_size(g_sysinfo.total_memory, buf); fb_puts(buf); fb_putc('\n');
    fb_puts("Usable memory:  ");
    fmt_size(g_sysinfo.usable_memory, buf); fb_puts(buf); fb_putc('\n');
    char cnt[12]; kitoa((int64_t)g_sysinfo.memmap_entries, cnt);
    fb_puts("Memory regions: "); fb_puts(cnt); fb_putc('\n');
}

static void cmd_dmesg(void) {
    char buf[32];
    fb_puts("[    0.000] SHENLONG OS v"); fb_puts(OS_VERSION); fb_puts(" booting\n");
    fb_puts("[    0.001] Limine Boot Protocol v3 active\n");

    if (g_sysinfo.firmware_type == 2)
        fb_puts("[    0.002] Firmware: UEFI 64-bit\n");
    else
        fb_puts("[    0.002] Firmware: BIOS\n");

    if (g_sysinfo.total_memory) {
        fmt_size(g_sysinfo.total_memory, buf);
        fb_puts("[    0.003] Memory: "); fb_puts(buf); fb_puts(" detected\n");
    }

    if (g_sysinfo.tsc_freq_hz) {
        fmt_freq(g_sysinfo.tsc_freq_hz, buf);
        fb_puts("[    0.004] TSC: "); fb_puts(buf); fb_puts(" calibrated\n");
    }

    fb_puts("[    0.005] IDT initialized\n");
    fb_puts("[    0.006] PS/2 keyboard controller initialized\n");
    fb_puts("[    0.007] VFS: rootfs mounted on /\n");
    fb_puts("[    0.008] VFS: tmpfs mounted on /tmp\n");
    fb_puts("[    0.009] VFS: tmpfs mounted on /sys\n");

    if (g_sysinfo.kernel_virt_base) {
        fb_puts("[    0.010] KASLR: kernel relocated to 0x");
        uint64_t addr = g_sysinfo.kernel_virt_base;
        char hex[17]; hex[16]='\0';
        for (int i=15;i>=0;i--) {
            uint8_t n=(uint8_t)(addr&0xF);
            hex[i]=(n<10)?('0'+n):('a'+n-10); addr>>=4;
        }
        char *h=hex; while(*h=='0'&&*(h+1)) h++;
        fb_puts(h); fb_putc('\n');
    }

    fb_puts("[    0.011] Shell started on tty0\n");
}

static void cmd_df(void) {
    char buf[32];
    fb_puts("Filesystem     Size    Used    Avail   Use%  Mounted on\n");
    if (fs_is_persistent()) {
        uint64_t disk_bytes = (uint64_t)ata_sector_count() * 512;
        if (!disk_bytes) disk_bytes = 64 * 1024 * 1024;
        fb_puts("/dev/ide0      ");
        fmt_size(disk_bytes, buf); fb_puts(buf); fb_puts("  ");
        fmt_size(1024 * 1024, buf); fb_puts(buf); fb_puts("  ");
        fmt_size(disk_bytes - 1024 * 1024, buf); fb_puts(buf);
        fb_puts("   2%   / (SHENFS02)\n");
    } else {
        fb_puts("rootfs         ");
        fmt_size(g_sysinfo.usable_memory ? g_sysinfo.usable_memory : 2*1024*1024, buf);
        fb_puts(buf); fb_puts("  ");
        fmt_size(256*1024, buf); fb_puts(buf); fb_puts("  ");
        uint64_t avail = (g_sysinfo.usable_memory > 256*1024) ?
                         g_sysinfo.usable_memory - 256*1024 : 0;
        fmt_size(avail, buf); fb_puts(buf);
        fb_puts("  12%   / (RAM)\n");
    }
    fb_puts("tmpfs          2 MB    0 B     2 MB    0%    /tmp\n");
    fb_puts("tmpfs          1 MB    0 B     1 MB    0%    /sys\n");
}

static void cmd_sync(int argc, char **argv) {
    (void)argc; (void)argv;
    if (fs_is_persistent()) {
        fs_sync();
        fb_puts("sync: Kalici diske senkronize edildi (/dev/ide0 - SHENFS02).\n");
        fb_puts("      Superblock, VFS dugumleri ve dosya bloklari diske yazildi.\n");
    } else {
        fb_puts("sync: RAM diski modu (Kalici IDE surucu bagli degil).\n");
    }
}

static void cmd_sshd(int argc, char **argv) {
    if (argc > 1) {
        if (!kstrcmp(argv[1], "start")) {
            ssh_sshd_enable(1);
            fb_puts("sshd: Port 22 dinleme baslatildi.\n");
            return;
        } else if (!kstrcmp(argv[1], "stop")) {
            ssh_sshd_enable(0);
            fb_puts("sshd: Sunucu durduruldu.\n");
            return;
        } else if (!kstrcmp(argv[1], "restart")) {
            ssh_sshd_enable(0);
            ssh_sshd_enable(1);
            fb_puts("sshd: Yeniden baslatildi (Port 22).\n");
            return;
        }
    }
    const ssh_status_t *st = ssh_get_status();
    fb_puts("OpenSSH-uyumlu SSH-2.0 Sunucusu (SHENLONG sshd):\n");
    fb_puts("  Durum:            ");
    fb_puts(st->running ? "CALISIYOR (Port 22 dinleniyor)\n" : "DURDURULDU\n");
    fb_puts("  Protokol:         SSH-2.0 & Dual-mode Raw Terminal (RFC 4253)\n");
    fb_puts("  Host Key:         RSA 1024-bit (/etc/ssh/ssh_host_rsa_key.pub)\n");
    fb_puts("  Aktif Baglanti:   ");
    fb_puts(st->connected ? "Evet\n" : "Yok\n");
    if (st->connected) {
        char ip_str[32];
        net_ip_text(st->peer_ip, ip_str);
        fb_puts("  Istemci:          ");
        fb_puts(ip_str);
        fb_puts(" (");
        fb_puts(st->client_version[0] ? st->client_version : "Raw CLI");
        fb_puts(")\n");
    }
    fb_puts("  Toplam Oturum:    ");
    char num[16];
    kitoa(st->session_count, num);
    fb_puts(num);
    fb_puts("\n  Transfer (RX/TX): ");
    kitoa(st->rx_bytes, num);
    fb_puts(num); fb_puts(" B / ");
    kitoa(st->tx_bytes, num);
    fb_puts(num); fb_puts(" B\n");
}

static void cmd_ssh(int argc, char **argv) {
    if (argc < 2) {
        fb_puts("Kullanim: ssh [kullanici@]host [port]\n");
        fb_puts("Ornek:    ssh 10.0.2.2\n");
        fb_puts("          ssh 127.0.0.1 2222\n");
        return;
    }
    const char *target = argv[1];
    const char *user = "root";
    char host_buf[64];
    const char *at = target;
    while (*at && *at != '@') at++;
    if (*at == '@') {
        int ulen = (int)(at - target);
        if (ulen > 0 && ulen < 32) {
            static char ubuf[32];
            kmemcpy(ubuf, target, ulen);
            ubuf[ulen] = '\0';
            user = ubuf;
        }
        target = at + 1;
    }
    int hlen = kstrlen(target);
    if (hlen > 63) hlen = 63;
    kmemcpy(host_buf, target, hlen);
    host_buf[hlen] = '\0';

    uint16_t port = 22;
    if (argc > 2) {
        int64_t p = katoi(argv[2]);
        if (p > 0 && p < 65536) port = (uint16_t)p;
    }
    ssh_client_start(host_buf, port, user, NULL);
}

static void cmd_ls(int argc, char **argv) {
    vfs_node_t *dir;
    if (argc > 1) {
        const char *path = argv[1];
        if (path[0] != '/') {
            char full[256]; get_cwd(full, 256);
            if (full[kstrlen(full)-1] != '/') kstrcat(full, "/");
            kstrcat(full, path); dir = fs_lookup(full);
        } else {
            dir = fs_lookup(path);
        }
    } else {
        /* Argümansız: mevcut dizini listele */
        dir = cwd_node ? cwd_node : fs_root;
    }
    if (!dir||dir->type!=NODE_DIR) { fb_puts("ls: bulunamadi\n"); return; }
    for (int i=0;i<dir->child_count;i++) {
        vfs_node_t *c=dir->children[i];
        uint16_t p=c->permissions;
        char ps[11];
        ps[0]=(c->type==NODE_DIR)?'d':'-';
        ps[1]=(p&PERM_OWNER_R)?'r':'-'; ps[2]=(p&PERM_OWNER_W)?'w':'-'; ps[3]=(p&PERM_OWNER_X)?'x':'-';
        ps[4]=(p&PERM_GROUP_R)?'r':'-'; ps[5]=(p&PERM_GROUP_W)?'w':'-'; ps[6]=(p&PERM_GROUP_X)?'x':'-';
        ps[7]=(p&PERM_OTHER_R)?'r':'-'; ps[8]=(p&PERM_OTHER_W)?'w':'-'; ps[9]=(p&PERM_OTHER_X)?'x':'-';
        ps[10]='\0';
        char sz[12]; kitoa((int64_t)c->size,sz);
        fb_puts(ps); fb_puts("  root  root  ");
        /* boyut sağa hizalı 6 karakter */
        int slen=(int)kstrlen(sz);
        for(int j=slen;j<6;j++) fb_putc(' ');
        fb_puts(sz); fb_puts("  ");
        fb_puts(c->name);
        if(c->type==NODE_DIR) fb_putc('/');
        fb_putc('\n');
    }
    if (dir->child_count==0) fb_puts("(bos)\n");
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc<2) { fb_puts("Kullanim: mkdir <ad>\n"); return; }
    vfs_node_t *parent=cwd_node?cwd_node:fs_root;
    fs_mkdir(parent,argv[1],PERM_DIR_DEFAULT);
    fb_puts("mkdir: '"); fb_puts(argv[1]); fb_puts("' olusturuldu\n");
}

static void cmd_touch(int argc, char **argv) {
    if (argc<2) { fb_puts("Kullanim: touch <ad>\n"); return; }
    vfs_node_t *parent=cwd_node?cwd_node:fs_root;
    fs_mkfile(parent,argv[1],PERM_FILE_DEFAULT);
}

static void cmd_cp(int argc, char **argv) {
    if (argc<3) { fb_puts("Kullanim: cp <src> <dst>\n"); return; }
    vfs_node_t *src=fs_lookup(argv[1]);
    if (!src||src->type==NODE_DIR) { fb_puts("cp: '"); fb_puts(argv[1]); fb_puts("': No such file\n"); return; }
    vfs_node_t *p=cwd_node?cwd_node:fs_root;
    vfs_node_t *dst=fs_mkfile(p,argv[2],src->permissions);
    fs_write(dst,src->data,src->size);
}

static void cmd_mv(int argc, char **argv) {
    if (argc<3) { fb_puts("Kullanim: mv <src> <dst>\n"); return; }
    vfs_node_t *p=cwd_node?cwd_node:fs_root;
    for (int i=0;i<p->child_count;i++) {
        if (kstrcmp(p->children[i]->name,argv[1])==0) {
            kstrcpy(p->children[i]->name,argv[2]); return;
        }
    }
    fb_puts("mv: cannot stat '"); fb_puts(argv[1]); fb_puts("': No such file or directory\n");
}

static void cmd_write(int argc, char **argv) {
    if (argc<3) { fb_puts("Kullanim: write <dosya> <icerik>\n"); return; }
    vfs_node_t *f=fs_lookup(argv[1]);
    if (!f) { vfs_node_t *p=cwd_node?cwd_node:fs_root; f=fs_mkfile(p,argv[1],PERM_FILE_DEFAULT); }
    fs_write(f,(uint8_t*)argv[2],(uint32_t)kstrlen(argv[2]));
}

static void cmd_pwd(void) {
    char path[256]; get_cwd(path,256); fb_puts(path); fb_putc('\n');
}

static void cmd_cd(int argc, char **argv) {
    if (argc<2) { cwd_node=fs_root; return; }
    const char *path=argv[1];
    vfs_node_t *t;
    if (path[0]=='/') {
        t=fs_lookup(path);
    } else if (kstrcmp(path,"..")==0) {
        t=(cwd_node&&cwd_node->parent)?cwd_node->parent:fs_root;
    } else if (kstrcmp(path,".")==0) { return; }
    else {
        char full[256]; get_cwd(full,256);
        if (full[kstrlen(full)-1]!='/') kstrcat(full,"/");
        kstrcat(full,path); t=fs_lookup(full);
    }
    if (!t||t->type!=NODE_DIR) { fb_puts("cd: "); fb_puts(path); fb_puts(": No such file or directory\n"); return; }
    cwd_node=t;
}

static void cmd_stat(int argc, char **argv) {
    if (argc<2) { fb_puts("Kullanim: stat <dosya>\n"); return; }
    vfs_node_t *n=fs_lookup(argv[1]);
    if (!n) { fb_puts("stat: cannot stat '"); fb_puts(argv[1]); fb_puts("': No such file or directory\n"); return; }
    char buf[21];
    fb_puts("  File: "); fb_puts(n->name); fb_putc('\n');
    kitoa((int64_t)n->size,buf);
    fb_puts("  Size: "); fb_puts(buf); fb_puts("\tType: ");
    fb_puts(n->type==NODE_DIR?"directory":"regular file"); fb_putc('\n');
    fb_puts("  Access: ("); 
    uint16_t p=n->permissions;
    char ps[10];
    ps[0]=(p&PERM_OWNER_R)?'r':'-'; ps[1]=(p&PERM_OWNER_W)?'w':'-'; ps[2]=(p&PERM_OWNER_X)?'x':'-';
    ps[3]=(p&PERM_GROUP_R)?'r':'-'; ps[4]=(p&PERM_GROUP_W)?'w':'-'; ps[5]=(p&PERM_GROUP_X)?'x':'-';
    ps[6]=(p&PERM_OTHER_R)?'r':'-'; ps[7]=(p&PERM_OTHER_W)?'w':'-'; ps[8]=(p&PERM_OTHER_X)?'x':'-';
    ps[9]='\0';
    fb_puts(ps); fb_puts(")  Uid: 0 (root)  Gid: 0 (root)\n");
}

static void cmd_chmod(int argc, char **argv) {
    if (argc<3) { fb_puts("Kullanim: chmod <octal> <dosya>\n"); return; }
    vfs_node_t *n=fs_lookup(argv[2]);
    if (!n) { fb_puts("chmod: cannot access '"); fb_puts(argv[2]); fb_puts("': No such file or directory\n"); return; }
    int64_t oct=katoi(argv[1]);
    int o=(int)((oct/100)%10),g=(int)((oct/10)%10),t=(int)(oct%10);
    uint16_t p=0;
    if(o&4)p|=PERM_OWNER_R; if(o&2)p|=PERM_OWNER_W; if(o&1)p|=PERM_OWNER_X;
    if(g&4)p|=PERM_GROUP_R; if(g&2)p|=PERM_GROUP_W; if(g&1)p|=PERM_GROUP_X;
    if(t&4)p|=PERM_OTHER_R; if(t&2)p|=PERM_OTHER_W; if(t&1)p|=PERM_OTHER_X;
    n->permissions=p;
}

static void cmd_env(void) {
    fb_puts("PATH=/bash:/bin:/usr/bin\n");
    fb_puts("HOME=/root\n");
    fb_puts("TERM=shenlong-vt\n");
    fb_puts("USER=root\n");
    fb_puts("LOGNAME=root\n");
    fb_puts("SHELL=/bash/shell\n");
    fb_puts("HOSTNAME="); fb_puts(HOSTNAME); fb_putc('\n');
    fb_puts("OS="); fb_puts(OS_NAME); fb_putc('\n');
    fb_puts("VERSION="); fb_puts(OS_VERSION); fb_putc('\n');
}

static void cmd_ps(void) {
    fb_puts("  PID TTY      STAT   TIME COMMAND\n");
    fb_puts("    1 ?        Ss     0:00 [kernel]\n");
    fb_puts("    2 tty0     S      0:00 /bash/shell\n");
}

static void cmd_history(void) {
    int start=(hist_count>HISTORY_SIZE)?hist_count-HISTORY_SIZE:0;
    for (int i=start;i<hist_count;i++) {
        char num[8]; kitoa((int64_t)(i+1),num);
        int len=(int)kstrlen(num);
        for(int j=len;j<4;j++) fb_putc(' ');
        fb_puts(num); fb_puts("  ");
        fb_puts(history[i%HISTORY_SIZE]); fb_putc('\n');
    }
}

static void cmd_reboot(void) {
    fb_puts("Broadcast message: the system is going down for reboot NOW!\n");
    uint8_t v;
    do { __asm__ volatile("inb $0x64,%0":"=a"(v)); } while(v&0x02);
    __asm__ volatile("outb %0,$0x64"::"a"((uint8_t)0xFE));
    for(;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* Yeni / Güçlendirilmiş komutlar                                    */
/* ------------------------------------------------------------------ */

/* grep <pattern> <dosya> */
static void cmd_grep(int argc, char **argv) {
    int show_line = 0, invert = 0, icase = 0;
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        const char *f = argv[ai]+1;
        while (*f) {
            if (*f=='n') show_line=1;
            else if (*f=='v') invert=1;
            else if (*f=='i') icase=1;
            f++;
        }
        ai++;
    }
    if (ai+1 > argc) { fb_puts("Kullanim: grep [-n] [-v] [-i] <kalip> <dosya>\n"); return; }
    const char *pat  = argv[ai++];
    const char *path = (ai < argc) ? argv[ai] : NULL;

    vfs_node_t *f_node = path ? fs_lookup(path) : NULL;
    if (!f_node || f_node->type == NODE_DIR) {
        fb_puts("grep: "); fb_puts(path ? path : ""); fb_puts(": dosya bulunamadi\n"); return;
    }

    /* icase için küçük harfe çevirmek zor — basit: sadece exact/case-sensitive */
    uint32_t lineno = 1;
    uint32_t i = 0;
    char line[256]; int llen = 0;
    while (i <= f_node->size) {
        char c = (i < f_node->size) ? (char)f_node->data[i] : '\n';
        i++;
        if (c == '\n' || c == '\0') {
            line[llen] = '\0';
            /* icase: basit lowercase copy */
            char lcline[256], lcpat[64];
            kstrcpy(lcline, line); kstrcpy(lcpat, pat);
            if (icase) {
                for (int k=0; lcline[k]; k++) if (lcline[k]>='A'&&lcline[k]<='Z') lcline[k]+=32;
                for (int k=0; lcpat[k];  k++) if (lcpat[k] >='A'&&lcpat[k] <='Z') lcpat[k] +=32;
            }
            int found = (kstrstr(lcline, lcpat) != NULL);
            if (found != invert) {
                if (show_line) { char nb[12]; kitoa((int64_t)lineno, nb); fb_puts(nb); fb_putc(':'); }
                fb_puts(line); fb_putc('\n');
            }
            lineno++; llen = 0;
        } else {
            if (llen < 255) line[llen++] = c;
        }
        if (c == '\0') break;
    }
    _unused(icase);
}

/* wc [-l] [-w] [-c] <dosya> */
static void cmd_wc(int argc, char **argv) {
    int do_l=1, do_w=1, do_c=1;
    int ai = 1;
    int flag_given = 0;
    while (ai < argc && argv[ai][0] == '-') {
        flag_given = 1; do_l=do_w=do_c=0;
        const char *f = argv[ai]+1;
        while (*f) { if(*f=='l') do_l=1; else if(*f=='w') do_w=1; else if(*f=='c') do_c=1; f++; }
        ai++;
    }
    _unused(flag_given);
    if (ai >= argc) { fb_puts("Kullanim: wc [-l] [-w] [-c] <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[ai]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("wc: dosya bulunamadi\n"); return; }

    uint32_t lines=0, words=0, bytes=fn->size;
    int in_word = 0;
    for (uint32_t i=0; i<fn->size; i++) {
        char c = (char)fn->data[i];
        if (c=='\n') lines++;
        if (c==' '||c=='\t'||c=='\n'||c=='\r') { in_word=0; }
        else if (!in_word) { in_word=1; words++; }
    }
    char buf[16];
    if (do_l) { kitoa((int64_t)lines, buf); fb_puts(buf); fb_putc(' '); }
    if (do_w) { kitoa((int64_t)words, buf); fb_puts(buf); fb_putc(' '); }
    if (do_c) { kitoa((int64_t)bytes, buf); fb_puts(buf); fb_putc(' '); }
    fb_puts(argv[ai]); fb_putc('\n');
}

/* head [-n N] <dosya> */
static void cmd_head(int argc, char **argv) {
    int n = 10, ai = 1;
    if (ai < argc && argv[ai][0]=='-' && argv[ai][1]=='n') {
        ai++;
        if (ai < argc) { n = (int)katoi(argv[ai++]); }
    }
    if (ai >= argc) { fb_puts("Kullanim: head [-n N] <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[ai]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("head: dosya bulunamadi\n"); return; }
    int lines = 0;
    for (uint32_t i=0; i<fn->size && lines<n; i++) {
        char c = (char)fn->data[i];
        fb_putc(c);
        if (c=='\n') lines++;
    }
}

/* tail [-n N] <dosya> */
static void cmd_tail(int argc, char **argv) {
    int n = 10, ai = 1;
    if (ai < argc && argv[ai][0]=='-' && argv[ai][1]=='n') {
        ai++;
        if (ai < argc) { n = (int)katoi(argv[ai++]); }
    }
    if (ai >= argc) { fb_puts("Kullanim: tail [-n N] <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[ai]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("tail: dosya bulunamadi\n"); return; }
    /* Son n satırı bul */
    int total_lines = 0;
    for (uint32_t i=0; i<fn->size; i++) if (fn->data[i]=='\n') total_lines++;
    int skip = total_lines - n;
    if (skip < 0) skip = 0;
    int skipped = 0;
    for (uint32_t i=0; i<fn->size; i++) {
        char c = (char)fn->data[i];
        if (skipped < skip) { if (c=='\n') skipped++; continue; }
        fb_putc(c);
    }
}

/* find [yol] [-name <kalip>] [-type f|d] */
static void cmd_find(int argc, char **argv) {
    const char *start_path = "/";
    const char *name_pat = NULL;
    int type_filter = 0; /* 0=hepsi 1=dosya 2=dizin */
    int ai = 1;
    if (ai < argc && argv[ai][0] != '-') { start_path = argv[ai++]; }
    while (ai < argc) {
        if (kstrcmp(argv[ai],"-name")==0 && ai+1<argc) { name_pat=argv[ai+1]; ai+=2; }
        else if (kstrcmp(argv[ai],"-type")==0 && ai+1<argc) {
            if (argv[ai+1][0]=='f') type_filter=1;
            else if (argv[ai+1][0]=='d') type_filter=2;
            ai+=2;
        } else ai++;
    }

    vfs_node_t *root = fs_lookup(start_path);
    if (!root) { fb_puts("find: yol bulunamadi\n"); return; }

    /* iteratif DFS — küçük stack (max derinlik 16) */
    vfs_node_t *stack[64]; int sp=0;
    char path_buf[256];
    kstrcpy(path_buf, start_path);
    stack[sp++] = root;

    /* Basit: sadece bir seviye derinlik (recursive VFS traverse) */
    /* Gerçek recursive iç fonksiyon */
    /* -- inline recursive helper as a manual stack -- */
    /* Yeniden: recursive function macro ile çöz */
    struct { vfs_node_t *n; char p[128]; } dstack[64];
    int dsp = 0;
    kstrcpy(dstack[0].p, start_path);
    dstack[0].n = root;
    dsp = 1;

    while (dsp > 0) {
        dsp--;
        vfs_node_t *cur = dstack[dsp].n;
        char cur_path[128]; kstrcpy(cur_path, dstack[dsp].p);

        /* Bu node için filtre */
        int type_ok = (type_filter==0) ||
                      (type_filter==1 && cur->type==NODE_FILE) ||
                      (type_filter==2 && cur->type==NODE_DIR);
        int name_ok = (!name_pat) || glob_match(name_pat, cur->name);

        if (type_ok && name_ok) { fb_puts(cur_path); fb_putc('\n'); }

        if (cur->type == NODE_DIR) {
            for (int i = cur->child_count-1; i >= 0 && dsp < 63; i--) {
                vfs_node_t *ch = cur->children[i];
                kstrcpy(dstack[dsp].p, cur_path);
                if (cur_path[kstrlen(cur_path)-1] != '/') kstrcat(dstack[dsp].p, "/");
                kstrcat(dstack[dsp].p, ch->name);
                dstack[dsp].n = ch;
                dsp++;
            }
        }
    }
    _unused(sp); _unused(stack[0]);
}

/* hexdump <dosya> */
static void cmd_hexdump(int argc, char **argv) {
    if (argc < 2) { fb_puts("Kullanim: hexdump <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[1]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("hexdump: dosya bulunamadi\n"); return; }
    uint32_t sz = fn->size;
    if (sz > 512) sz = 512; /* max 512 byte goster */
    for (uint32_t i = 0; i < sz; i += 16) {
        /* offset */
        char ob[9];
        uint32_t off = i;
        ob[8]='\0';
        for (int k=7;k>=0;k--) { ob[k]="0123456789abcdef"[off&0xf]; off>>=4; }
        fb_puts(ob); fb_puts(":  ");
        for (uint32_t j=i; j<i+16; j++) {
            if (j < fn->size) put_hex2(fn->data[j]);
            else fb_puts("  ");
            fb_putc(' ');
            if (j==i+7) fb_putc(' ');
        }
        fb_puts(" |");
        for (uint32_t j=i; j<i+16 && j<fn->size; j++) {
            char c = (char)fn->data[j];
            fb_putc((c>=0x20 && c<0x7f) ? c : '.');
        }
        fb_puts("|\n");
    }
    if (fn->size > 512) fb_puts("... (sadece ilk 512 byte gosterildi)\n");
}

/* whoami */
static void cmd_whoami(void) { fb_puts("root\n"); }

/* hostname */
static void cmd_hostname(void) { fb_puts(HOSTNAME); fb_putc('\n'); }

/* seq <son> veya seq <ilk> <son> veya seq <ilk> <artis> <son> */
static void cmd_seq(int argc, char **argv) {
    if (argc < 2) { fb_puts("Kullanim: seq [ilk [artis]] son\n"); return; }
    int64_t first=1, step=1, last;
    if (argc == 2) { last = katoi(argv[1]); }
    else if (argc == 3) { first = katoi(argv[1]); last = katoi(argv[2]); }
    else { first = katoi(argv[1]); step = katoi(argv[2]); last = katoi(argv[3]); }
    if (step == 0) { fb_puts("seq: artis sifir olamaz\n"); return; }
    char buf[21];
    if (step > 0) { for (int64_t i=first; i<=last; i+=step) { kitoa(i,buf); fb_puts(buf); fb_putc('\n'); } }
    else          { for (int64_t i=first; i>=last; i+=step) { kitoa(i,buf); fb_puts(buf); fb_putc('\n'); } }
}

/* alias [ad=deger] */
static void cmd_alias(int argc, char **argv) {
    if (argc < 2) {
        for (int i=0; i<alias_count; i++) {
            fb_puts("alias "); fb_puts(alias_name[i]); fb_puts("='"); fb_puts(alias_val[i]); fb_puts("'\n");
        }
        return;
    }
    /* ad=deger formatı */
    char *eq = NULL;
    for (char *p = argv[1]; *p; p++) { if (*p == '=') { eq = p; break; } }
    if (!eq) { fb_puts("alias: Kullanim: alias ad=deger\n"); return; }
    *eq = '\0';
    const char *name = argv[1];
    const char *val  = eq+1;
    /* Mevcut alias güncelle */
    for (int i=0; i<alias_count; i++) {
        if (kstrcmp(alias_name[i], name)==0) { kstrcpy(alias_val[i], val); return; }
    }
    if (alias_count < ALIAS_MAX) {
        kstrcpy(alias_name[alias_count], name);
        kstrcpy(alias_val[alias_count], val);
        alias_count++;
    } else { fb_puts("alias: tablo dolu\n"); }
}

/* unalias <ad> */
static void cmd_unalias(int argc, char **argv) {
    if (argc < 2) { fb_puts("Kullanim: unalias <ad>\n"); return; }
    for (int i=0; i<alias_count; i++) {
        if (kstrcmp(alias_name[i], argv[1])==0) {
            for (int j=i; j<alias_count-1; j++) {
                kstrcpy(alias_name[j], alias_name[j+1]);
                kstrcpy(alias_val[j],  alias_val[j+1]);
            }
            alias_count--;
            return;
        }
    }
    fb_puts("unalias: bulunamadi: "); fb_puts(argv[1]); fb_putc('\n');
}

/* export [AD=DEGER] / printenv / unset */
#define ENV_MAX 16
static char env_key[ENV_MAX][32];
static char env_val[ENV_MAX][128];
static int  env_count = 0;

static const char *env_get(const char *k) {
    for (int i=0; i<env_count; i++) if (kstrcmp(env_key[i],k)==0) return env_val[i];
    return NULL;
}

static void cmd_export(int argc, char **argv) {
    if (argc < 2) {
        for (int i=0; i<env_count; i++) {
            fb_puts("export "); fb_puts(env_key[i]); fb_putc('='); fb_puts(env_val[i]); fb_putc('\n');
        }
        return;
    }
    char *eq = NULL;
    for (char *p = argv[1]; *p; p++) { if (*p == '=') { eq = p; break; } }
    if (!eq) { fb_puts("export: Kullanim: export AD=DEGER\n"); return; }
    *eq = '\0';
    for (int i=0; i<env_count; i++) {
        if (kstrcmp(env_key[i], argv[1])==0) { kstrcpy(env_val[i], eq+1); return; }
    }
    if (env_count < ENV_MAX) {
        kstrcpy(env_key[env_count], argv[1]);
        kstrcpy(env_val[env_count], eq+1);
        env_count++;
    }
}

static void cmd_printenv(int argc, char **argv) {
    if (argc > 1) {
        const char *v = env_get(argv[1]);
        if (v) { fb_puts(v); fb_putc('\n'); }
        else { fb_puts("printenv: "); fb_puts(argv[1]); fb_puts(": tanimli degil\n"); }
        return;
    }
    /* Yerleşik + kullanıcı tanımlı */
    fb_puts("PATH=/bash:/bin:/usr/bin\nHOME=/root\nTERM=shenlong-vt\nUSER=root\n");
    fb_puts("SHELL=/bash/shell\nHOSTNAME="); fb_puts(HOSTNAME); fb_putc('\n');
    for (int i=0; i<env_count; i++) { fb_puts(env_key[i]); fb_putc('='); fb_puts(env_val[i]); fb_putc('\n'); }
}

static void cmd_unset(int argc, char **argv) {
    if (argc < 2) { fb_puts("Kullanim: unset <AD>\n"); return; }
    for (int i=0; i<env_count; i++) {
        if (kstrcmp(env_key[i], argv[1])==0) {
            for (int j=i; j<env_count-1; j++) {
                kstrcpy(env_key[j], env_key[j+1]);
                kstrcpy(env_val[j], env_val[j+1]);
            }
            env_count--;
            return;
        }
    }
}

/* true / false */
static void cmd_true(void)  { /* exit 0 — çıkış kodu kavramı yok ama komut var olsun */ }
static void cmd_false(void) { /* exit 1 */ }

/* yes [metin] — ctrl+c olmadığından max 20 satır */
static void cmd_yes(int argc, char **argv) {
    const char *msg = (argc > 1) ? argv[1] : "y";
    for (int i=0; i<20; i++) { fb_puts(msg); fb_putc('\n'); }
    fb_puts("(20 satir ile sinirlandirildi)\n");
}

/* tty */
static void cmd_tty(void) { fb_puts("/dev/tty0\n"); }

/* id */
static void cmd_id(void) {
    fb_puts("uid=0(root) gid=0(root) groups=0(root)\n");
}

/* ls güçlendirme — -l -a -la destekli wrapper */
static void cmd_ls_ext(int argc, char **argv) {
    /* -l, -a, -la, -al bayraklarını tüket; geri kalanı cmd_ls'e ver */
    /* Mevcut cmd_ls zaten -l benzeri çıktı veriyor; sadece -a (gizli) ekleyelim */
    /* Basitlik için direkt cmd_ls'i çağır — VFS'de gizli dosya kavramı yok */
    cmd_ls(argc, argv);
}

/* rm -r <dizin> destekli sürüm */
static void cmd_rm_ext(int argc, char **argv) {
    int recursive = 0, ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        for (char *f = argv[ai]+1; *f; f++) if (*f=='r'||*f=='R') recursive=1;
        ai++;
    }
    if (ai >= argc) { fb_puts("Kullanim: rm [-r] <ad>\n"); return; }
    /* Recursive silme için basit: önce çocukları sil */
    vfs_node_t *parent = cwd_node ? cwd_node : fs_root;
    for (int i=0; i<parent->child_count; i++) {
        if (kstrcmp(parent->children[i]->name, argv[ai])==0) {
            vfs_node_t *target = parent->children[i];
            if (target->type == NODE_DIR && !recursive) {
                fb_puts("rm: '"); fb_puts(argv[ai]); fb_puts("' bir dizin (rm -r kullanin)\n"); return;
            }
            /* çocukları temizle */
            target->child_count = 0;
            for (int j=i; j<parent->child_count-1; j++) parent->children[j]=parent->children[j+1];
            parent->child_count--;
            return;
        }
    }
    fb_puts("rm: cannot remove '"); fb_puts(argv[ai]); fb_puts("': No such file or directory\n");
}

/* echo -n desteği */
static void cmd_echo_ext(int argc, char **argv) {
    int no_nl = 0, ai = 1;
    if (ai < argc && kstrcmp(argv[ai], "-n")==0) { no_nl=1; ai++; }
    for (int i=ai; i<argc; i++) { fb_puts(argv[i]); if(i<argc-1) fb_putc(' '); }
    if (!no_nl) fb_putc('\n');
}

/* cat çok dosya desteği */
static void cmd_cat_ext(int argc, char **argv) {
    if (argc < 2) { fb_puts("Kullanim: cat <dosya> [dosya2 ...]\n"); return; }
    for (int fi=1; fi<argc; fi++) {
        vfs_node_t *f = fs_lookup(argv[fi]);
        if (!f || f->type == NODE_DIR) { fb_puts("cat: "); fb_puts(argv[fi]); fb_puts(": Dosya bulunamadi\n"); continue; }
        if (!perm_check(f,0,1,0,0)) { fb_puts("cat: "); fb_puts(argv[fi]); fb_puts(": Izin reddedildi\n"); continue; }
        for (uint32_t i=0; i<f->size; i++) fb_putc((char)f->data[i]);
        if (f->size==0) fb_puts("(bos dosya)\n");
        else if (f->data[f->size-1] != '\n') fb_putc('\n');
    }
}

/* ------------------------------------------------------------------ */
/* Yeni komutlar: sort, uniq, cut, tr, printf, pushd/popd/dirs, test */
/* ------------------------------------------------------------------ */

/* Directory stack */
#define DIR_STACK_MAX 16
static char dir_stack[DIR_STACK_MAX][256];
static int  dir_sp = 0;

static void cmd_pushd(int argc, char **argv) {
    if (argc < 2) { fb_puts("pushd: dizin gerekli\n"); return; }
    if (dir_sp >= DIR_STACK_MAX-1) { fb_puts("pushd: stack dolu\n"); return; }
    /* Mevcut dizini kaydet */
    get_cwd(dir_stack[dir_sp], 256);
    dir_sp++;
    /* cd yap */
    char *cd_argv[2] = { "cd", argv[1] };
    cmd_cd(2, cd_argv);
    /* yeni dizini de göster */
    char path[256]; get_cwd(path, 256);
    fb_puts(path); fb_putc('\n');
}

static void cmd_popd(void) {
    if (dir_sp <= 0) { fb_puts("popd: stack bos\n"); return; }
    dir_sp--;
    char *cd_argv[2] = { "cd", dir_stack[dir_sp] };
    cmd_cd(2, cd_argv);
    char path[256]; get_cwd(path, 256);
    fb_puts(path); fb_putc('\n');
}

static void cmd_dirs(void) {
    char cur[256]; get_cwd(cur, 256);
    fb_puts(cur);
    for (int i = dir_sp-1; i >= 0; i--) { fb_putc(' '); fb_puts(dir_stack[i]); }
    fb_putc('\n');
}

/* sort: her satırı sırala (basit insertion sort) */
static void cmd_sort_buf(const char *data, uint32_t size, int reverse) {
    /* Satırları ayır, sırala, yaz */
    char lines[64][128]; int nl = 0;
    uint32_t i = 0, llen = 0;
    while (i <= size && nl < 64) {
        char c = (i < size) ? (char)data[i] : '\n'; i++;
        if (c == '\n' || c == '\0') {
            lines[nl][llen] = '\0'; nl++; llen = 0;
        } else { if (llen < 127) lines[nl][llen++] = c; }
        if (c == '\0') break;
    }
    /* Insertion sort */
    for (int a = 1; a < nl; a++) {
        char tmp[128]; kstrcpy(tmp, lines[a]);
        int b = a-1;
        while (b >= 0) {
            int cmp = 0;
            const char *p = lines[b], *q = tmp;
            while (*p && *q && *p == *q) { p++; q++; }
            cmp = (unsigned char)*p - (unsigned char)*q;
            if ((!reverse && cmp > 0) || (reverse && cmp < 0)) {
                kstrcpy(lines[b+1], lines[b]); b--;
            } else break;
        }
        kstrcpy(lines[b+1], tmp);
    }
    for (int k = 0; k < nl; k++) { fb_puts(lines[k]); fb_putc('\n'); }
}

static void cmd_sort(int argc, char **argv) {
    int rev = 0, ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        for (char *f=argv[ai]+1; *f; f++) if (*f=='r') rev=1;
        ai++;
    }
    if (ai >= argc) { fb_puts("Kullanim: sort [-r] <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[ai]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("sort: dosya bulunamadi\n"); return; }
    cmd_sort_buf((const char*)fn->data, fn->size, rev);
}

/* uniq: ardışık tekrar satırları kaldır */
static void cmd_uniq(int argc, char **argv) {
    int count = 0, ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        for (char *f=argv[ai]+1; *f; f++) if (*f=='c') count=1;
        ai++;
    }
    if (ai >= argc) { fb_puts("Kullanim: uniq [-c] <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[ai]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("uniq: dosya bulunamadi\n"); return; }
    char prev[256]; prev[0] = '\0';
    char cur[256]; int clen = 0;
    int rep = 0;
    uint32_t i = 0;
    while (i <= fn->size) {
        char c = (i < fn->size) ? (char)fn->data[i] : '\n'; i++;
        if (c == '\n' || c == '\0') {
            cur[clen] = '\0';
            if (kstrcmp(cur, prev) != 0) {
                if (count && rep > 0) {
                    char nb[8]; kitoa((int64_t)rep, nb);
                    int l = (int)kstrlen(nb);
                    for (int j=l; j<6; j++) fb_putc(' ');
                    fb_puts(nb); fb_putc(' ');
                }
                fb_puts(cur); fb_putc('\n');
                kstrcpy(prev, cur); rep = 1;
            } else { rep++; }
            clen = 0;
        } else { if (clen < 255) cur[clen++] = c; }
        if (c == '\0') break;
    }
}

/* cut -d<delim> -f<field> <dosya> */
static void cmd_cut(int argc, char **argv) {
    char delim = '\t'; int field = 1; int ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        char *f = argv[ai]+1;
        if (*f == 'd') { delim = *(f+1) ? *(f+1) : (ai+1<argc ? argv[++ai][0] : '\t'); }
        else if (*f == 'f') { field = (int)katoi(f+1 ? f+1 : (ai+1<argc ? argv[++ai] : "1")); }
        ai++;
    }
    if (ai >= argc) { fb_puts("Kullanim: cut -d<delim> -f<N> <dosya>\n"); return; }
    vfs_node_t *fn = fs_lookup(argv[ai]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("cut: dosya bulunamadi\n"); return; }
    char line[256]; int llen = 0;
    for (uint32_t i = 0; i <= fn->size; i++) {
        char c = (i < fn->size) ? (char)fn->data[i] : '\n';
        if (c == '\n' || c == '\0') {
            line[llen] = '\0';
            /* delimeter ile böl, field'ı bul */
            int fnum = 1; char *p = line;
            while (*p) {
                char *start = p;
                while (*p && *p != delim) p++;
                if (fnum == field) {
                    char tmp = *p; *p = '\0'; fb_puts(start); *p = tmp; fb_putc('\n'); break;
                }
                if (*p) p++;
                fnum++;
            }
            llen = 0;
        } else { if (llen < 255) line[llen++] = c; }
        if (c == '\0') break;
    }
}

/* tr <set1> <set2>  (sadece stdin yerine dosya, basit char map) */
static void cmd_tr(int argc, char **argv) {
    if (argc < 4) { fb_puts("Kullanim: tr <set1> <set2> <dosya>\n"); return; }
    const char *s1 = argv[1], *s2 = argv[2];
    vfs_node_t *fn = fs_lookup(argv[3]);
    if (!fn || fn->type == NODE_DIR) { fb_puts("tr: dosya bulunamadi\n"); return; }
    for (uint32_t i = 0; i < fn->size; i++) {
        char c = (char)fn->data[i];
        int found = 0;
        for (int k = 0; s1[k]; k++) {
            if (c == s1[k]) {
                /* s2'de k. karakter var mı? */
                int l = (int)kstrlen(s2);
                fb_putc(k < l ? s2[k] : s2[l-1]);
                found = 1; break;
            }
        }
        if (!found) fb_putc(c);
    }
}

/* printf <format> [args...] — basit %s %d %i %x %c %% destekli */
static void cmd_printf(int argc, char **argv) {
    if (argc < 2) { fb_puts("Kullanim: printf <format> [arg ...]\n"); return; }
    const char *fmt = argv[1];
    int ai = 2;
    char buf[32];
    for (const char *p = fmt; *p; p++) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') fb_putc('\n');
            else if (*p == 't') fb_putc('\t');
            else if (*p == '\\') fb_putc('\\');
            else { fb_putc('\\'); fb_putc(*p); }
        } else if (*p == '%') {
            p++;
            if (*p == '%') { fb_putc('%'); }
            else if (*p == 's') { if (ai < argc) fb_puts(argv[ai++]); }
            else if (*p == 'd' || *p == 'i') {
                if (ai < argc) { kitoa(katoi(argv[ai++]), buf); fb_puts(buf); }
            }
            else if (*p == 'x') {
                if (ai < argc) {
                    uint64_t v = (uint64_t)katoi(argv[ai++]);
                    char hex[17]; hex[16]='\0';
                    for (int k=15;k>=0;k--) { hex[k]="0123456789abcdef"[v&0xf]; v>>=4; }
                    char *h=hex; while(*h=='0'&&*(h+1)) h++;
                    fb_puts(h);
                }
            }
            else if (*p == 'c') { if (ai < argc) fb_putc(argv[ai++][0]); }
            else { fb_putc('%'); fb_putc(*p); }
        } else { fb_putc(*p); }
    }
}

/* test / [ — basit ifadeler: -f -d -e -z -n = != -eq -ne -lt -le -gt -ge */
static int cmd_test_eval(int argc, char **argv) {
    /* test'e gelen argümanlar argv[1]'den başlar */
    if (argc < 2) return 1; /* false */
    if (argc == 2) {
        /* test <string> */
        return (argv[1][0] == '\0') ? 1 : 0;
    }
    if (argc == 3) {
        const char *op = argv[1]; const char *val = argv[2];
        if (kstrcmp(op,"-z")==0) return val[0] ? 1 : 0;
        if (kstrcmp(op,"-n")==0) return val[0] ? 0 : 1;
        if (kstrcmp(op,"-f")==0) { vfs_node_t *n=fs_lookup(val); return (n&&n->type==NODE_FILE)?0:1; }
        if (kstrcmp(op,"-d")==0) { vfs_node_t *n=fs_lookup(val); return (n&&n->type==NODE_DIR)?0:1; }
        if (kstrcmp(op,"-e")==0) { return fs_lookup(val)?0:1; }
        if (kstrcmp(op,"!")==0)  { char *a2[2]={argv[0],argv[2]}; return !cmd_test_eval(2,a2); }
    }
    if (argc == 4) {
        const char *a = argv[1], *op = argv[2], *b = argv[3];
        if (kstrcmp(op,"=")==0 || kstrcmp(op,"==")==0) return kstrcmp(a,b)==0?0:1;
        if (kstrcmp(op,"!=")==0) return kstrcmp(a,b)!=0?0:1;
        int64_t ia=katoi(a), ib=katoi(b);
        if (kstrcmp(op,"-eq")==0) return ia==ib?0:1;
        if (kstrcmp(op,"-ne")==0) return ia!=ib?0:1;
        if (kstrcmp(op,"-lt")==0) return ia< ib?0:1;
        if (kstrcmp(op,"-le")==0) return ia<=ib?0:1;
        if (kstrcmp(op,"-gt")==0) return ia> ib?0:1;
        if (kstrcmp(op,"-ge")==0) return ia>=ib?0:1;
    }
    return 1;
}

static int g_last_exit = 0; /* son komutun exit kodu */

static void cmd_test(int argc, char **argv) {
    g_last_exit = cmd_test_eval(argc, argv);
}

/* ------------------------------------------------------------------ */
/* Prompt                                                             */
/* ------------------------------------------------------------------ */
static void print_prompt(void) {
    char path[256]; get_cwd(path,256);
    fb_puts("root@"); fb_puts(HOSTNAME); fb_putc(':');
    fb_puts(path); fb_puts("# ");
}

/* ------------------------------------------------------------------ */
/* Tek komut çalıştırıcı (pipe/redirect öncesi çözülmüş)             */
/* ------------------------------------------------------------------ */
static void run_single(char *line) {
    if (!line||!line[0]||line[0]=='#') return;
    char *argv[MAX_ARGS];
    int argc=parse_args(line,argv);
    if (argc==0) return;

    /* Alias genişlet */
    for (int ai=0; ai<alias_count; ai++) {
        if (kstrcmp(argv[0], alias_name[ai])==0) {
            static char alias_buf[CMD_BUF_SIZE];
            kstrcpy(alias_buf, alias_val[ai]);
            for (int k=1; k<argc && kstrlen(alias_buf)<CMD_BUF_SIZE-32; k++) {
                kstrcat(alias_buf, " "); kstrcat(alias_buf, argv[k]);
            }
            run_single(alias_buf);
            return;
        }
    }

    if      (!kstrcmp(argv[0],"help"))      cmd_help();
    else if (!kstrcmp(argv[0],"uname"))     cmd_uname(argc,argv);
    else if (!kstrcmp(argv[0],"date"))      cmd_date();
    else if (!kstrcmp(argv[0],"uptime"))    cmd_uptime();
    else if (!kstrcmp(argv[0],"perf"))      cmd_perf(argc,argv);
    else if (!kstrcmp(argv[0],"net"))       cmd_net(argc,argv);
    else if (!kstrcmp(argv[0],"web"))       cmd_web(argc,argv);
    else if (!kstrcmp(argv[0],"video"))     cmd_video(argc,argv);
    else if (!kstrcmp(argv[0],"free"))      cmd_free();
    else if (!kstrcmp(argv[0],"lscpu"))     cmd_lscpu();
    else if (!kstrcmp(argv[0],"lsmem"))     cmd_lsmem();
    else if (!kstrcmp(argv[0],"dmesg"))     cmd_dmesg();
    else if (!kstrcmp(argv[0],"df"))        cmd_df();
    else if (!kstrcmp(argv[0],"sync"))      cmd_sync(argc,argv);
    else if (!kstrcmp(argv[0],"ssh"))       cmd_ssh(argc,argv);
    else if (!kstrcmp(argv[0],"sshd"))      cmd_sshd(argc,argv);
    else if (!kstrcmp(argv[0],"echo"))      cmd_echo_ext(argc,argv);
    else if (!kstrcmp(argv[0],"ls"))        cmd_ls_ext(argc,argv);
    else if (!kstrcmp(argv[0],"cat"))       cmd_cat_ext(argc,argv);
    else if (!kstrcmp(argv[0],"mkdir"))     cmd_mkdir(argc,argv);
    else if (!kstrcmp(argv[0],"touch"))     cmd_touch(argc,argv);
    else if (!kstrcmp(argv[0],"rm"))        cmd_rm_ext(argc,argv);
    else if (!kstrcmp(argv[0],"cp"))        cmd_cp(argc,argv);
    else if (!kstrcmp(argv[0],"mv"))        cmd_mv(argc,argv);
    else if (!kstrcmp(argv[0],"write"))     cmd_write(argc,argv);
    else if (!kstrcmp(argv[0],"pwd"))       cmd_pwd();
    else if (!kstrcmp(argv[0],"cd"))        cmd_cd(argc,argv);
    else if (!kstrcmp(argv[0],"stat"))      cmd_stat(argc,argv);
    else if (!kstrcmp(argv[0],"chmod"))     cmd_chmod(argc,argv);
    else if (!kstrcmp(argv[0],"env"))       cmd_env();
    else if (!kstrcmp(argv[0],"ps"))        cmd_ps();
    else if (!kstrcmp(argv[0],"history"))   cmd_history();
    else if (!kstrcmp(argv[0],"clear"))     fb_clear(TERM_BG);
    else if (!kstrcmp(argv[0],"reboot"))    cmd_reboot();
    else if (!kstrcmp(argv[0],"grep"))      cmd_grep(argc,argv);
    else if (!kstrcmp(argv[0],"wc"))        cmd_wc(argc,argv);
    else if (!kstrcmp(argv[0],"head"))      cmd_head(argc,argv);
    else if (!kstrcmp(argv[0],"tail"))      cmd_tail(argc,argv);
    else if (!kstrcmp(argv[0],"find"))      cmd_find(argc,argv);
    else if (!kstrcmp(argv[0],"hexdump"))   cmd_hexdump(argc,argv);
    else if (!kstrcmp(argv[0],"whoami"))    cmd_whoami();
    else if (!kstrcmp(argv[0],"hostname"))  cmd_hostname();
    else if (!kstrcmp(argv[0],"id"))        cmd_id();
    else if (!kstrcmp(argv[0],"tty"))       cmd_tty();
    else if (!kstrcmp(argv[0],"seq"))       cmd_seq(argc,argv);
    else if (!kstrcmp(argv[0],"sort"))      cmd_sort(argc,argv);
    else if (!kstrcmp(argv[0],"uniq"))      cmd_uniq(argc,argv);
    else if (!kstrcmp(argv[0],"cut"))       cmd_cut(argc,argv);
    else if (!kstrcmp(argv[0],"tr"))        cmd_tr(argc,argv);
    else if (!kstrcmp(argv[0],"printf"))    cmd_printf(argc,argv);
    else if (!kstrcmp(argv[0],"pushd"))     cmd_pushd(argc,argv);
    else if (!kstrcmp(argv[0],"popd"))      cmd_popd();
    else if (!kstrcmp(argv[0],"dirs"))      cmd_dirs();
    else if (!kstrcmp(argv[0],"test"))      cmd_test(argc,argv);
    else if (!kstrcmp(argv[0],"["))         cmd_test(argc,argv);
    else if (!kstrcmp(argv[0],"alias"))     cmd_alias(argc,argv);
    else if (!kstrcmp(argv[0],"unalias"))   cmd_unalias(argc,argv);
    else if (!kstrcmp(argv[0],"export"))    cmd_export(argc,argv);
    else if (!kstrcmp(argv[0],"printenv"))  cmd_printenv(argc,argv);
    else if (!kstrcmp(argv[0],"unset"))     cmd_unset(argc,argv);
    else if (!kstrcmp(argv[0],"yes"))       cmd_yes(argc,argv);
    else if (!kstrcmp(argv[0],"true"))      { cmd_true(); g_last_exit=0; }
    else if (!kstrcmp(argv[0],"false"))     { cmd_false(); g_last_exit=1; }
    else if (!kstrcmp(argv[0],"exit"))      { for(;;) __asm__ volatile("hlt"); }
    else { fb_puts(argv[0]); fb_puts(": command not found\n"); g_last_exit=127; }
}

/* ------------------------------------------------------------------ */
/* Pipe buffer — tek zincir için yeterli                              */
/* ------------------------------------------------------------------ */
#define PIPE_BUF_SIZE (8*1024)
/* pipe_buf: şu an chain_buf/redir_cap tarafından karşılanıyor */

/* Bir segmenti çalıştır; pipe_in varsa /tmp/__pipe__ olarak VFS'e yaz */
static void run_segment(char *seg, const char *pipe_in, int capture_out,
                         char *cap_dst, uint32_t cap_sz) {
    /* Önce pipe_in'i /tmp/__pipe__'a yaz */
    if (pipe_in) {
        vfs_node_t *pf = fs_lookup("/tmp/__pipe__");
        if (!pf) {
            vfs_node_t *tmp = fs_lookup("/tmp");
            if (tmp) pf = fs_mkfile(tmp, "__pipe__", PERM_FILE_DEFAULT);
        }
        if (pf) fs_write(pf, (const uint8_t*)pipe_in, (uint32_t)kstrlen(pipe_in));
        /* Argümanlara __pipe__ dosyasını ekle (son argüman olarak) */
        /* Sadece birkaç komut pipe'ı anlıyor; genel çözüm için:
           seg sonuna " /tmp/__pipe__" ekle */
        static char seg_ext[CMD_BUF_SIZE];
        kstrcpy(seg_ext, seg);
        kstrcat(seg_ext, " /tmp/__pipe__");
        seg = seg_ext;
    }

    /* Boşlukları temizle */
    while (*seg == ' ') seg++;

    if (capture_out && cap_dst) {
        fb_capture_start(cap_dst, cap_sz);
        run_single(seg);
        fb_capture_end();
    } else {
        run_single(seg);
    }
}

/* ------------------------------------------------------------------ */
/* Komut yürütücü: ;  |  >  >>  &&  || destekli                       */
/* ------------------------------------------------------------------ */
static void execute(char *line) {
    if (!line || !line[0] || line[0] == '#') return;

    /* Semicolon: "cmd1 ; cmd2" → sırayla çalıştır */
    /* Önce ';' için böl */
    static char semi_parts[8][CMD_BUF_SIZE];
    int sn = 0;
    {
        char tmp[CMD_BUF_SIZE]; kstrcpy(tmp, line);
        char *p = tmp;
        while (*p && sn < 8) {
            char *start = p;
            while (*p && *p != ';') p++;
            int plen = (int)(p - start);
            if (plen > 0) {
                /* trim leading spaces */
                while (*start == ' ') { start++; plen--; }
                /* trim trailing spaces */
                while (plen > 0 && start[plen-1] == ' ') plen--;
                if (plen > 0) {
                    kmemcpy(semi_parts[sn], start, (uint32_t)plen);
                    semi_parts[sn][plen] = '\0';
                    sn++;
                }
            }
            if (*p == ';') p++;
        }
    }

    for (int si = 0; si < sn; si++) {
        char *seg = semi_parts[si];

        /* && ve || */
        char *and_pos = NULL, *or_pos = NULL;
        for (char *p = seg; *p; p++) {
            if (p[0]=='&' && p[1]=='&' && !and_pos) { and_pos=p; break; }
            if (p[0]=='|' && p[1]=='|' && !or_pos)  { or_pos=p; break; }
        }
        if (and_pos) {
            *and_pos = '\0'; char *right = and_pos+2;
            static char left_part[CMD_BUF_SIZE];
            kstrcpy(left_part, seg);
            execute(left_part);
            if (g_last_exit == 0) execute(right);
            continue;
        }
        if (or_pos) {
            *or_pos = '\0'; char *right = or_pos+2;
            static char left_part2[CMD_BUF_SIZE];
            kstrcpy(left_part2, seg);
            execute(left_part2);
            if (g_last_exit != 0) execute(right);
            continue;
        }

        /* Output redirect: >> veya > */
        char *append_pos = NULL, *redir_pos = NULL;
        for (char *p = seg; *p; p++) {
            if (p[0]=='>' && p[1]=='>') { append_pos=p; break; }
            if (p[0]=='>' && p[1]!='>')  { redir_pos=p; break; }
        }
        if (append_pos || redir_pos) {
            char *rp = append_pos ? append_pos : redir_pos;
            int append = (append_pos != NULL);
            *rp = '\0';
            char *fname = rp + (append ? 2 : 1);
            while (*fname == ' ') fname++;
            /* trim fname end */
            char *fe = fname; while (*fe && *fe!=' ') fe++; *fe='\0';
            /* trim cmd end */
            char *ce = rp-1; while (ce > seg && *ce==' ') { *ce='\0'; ce--; }

            static char redir_cap[PIPE_BUF_SIZE];
            fb_capture_start(redir_cap, PIPE_BUF_SIZE);
            run_single(seg);
            fb_capture_end();

            /* Dosyaya yaz */
            vfs_node_t *f = fs_lookup(fname);
            if (!f) {
                vfs_node_t *p2 = cwd_node ? cwd_node : fs_root;
                /* basename al */
                const char *bn = fname;
                for (const char *x=fname; *x; x++) if (*x=='/') bn=x+1;
                f = fs_mkfile(p2, bn, PERM_FILE_DEFAULT);
            }
            if (f) {
                if (append) {
                    /* mevcut içeriğe ekle */
                    uint32_t old = f->size;
                    uint32_t add = (uint32_t)kstrlen(redir_cap);
                    static uint8_t append_tmp[PIPE_BUF_SIZE];
                    if (old + add < PIPE_BUF_SIZE) {
                        kmemcpy(append_tmp, f->data, old);
                        kmemcpy(append_tmp+old, redir_cap, add);
                        fs_write(f, append_tmp, old+add);
                    }
                } else {
                    fs_write(f, (const uint8_t*)redir_cap, (uint32_t)kstrlen(redir_cap));
                }
            }
            g_last_exit = 0;
            continue;
        }

        /* Pipe: cmd1 | cmd2 | cmd3 */
        char *pipe_parts[8]; int pn = 0;
        {
            char *p = seg;
            pipe_parts[pn++] = p;
            while (*p && pn < 8) {
                if (*p == '|') { *p = '\0'; p++; pipe_parts[pn++] = p; }
                else p++;
            }
        }
        if (pn == 1) {
            run_single(seg);
        } else {
            /* Zincir: her komutun çıktısı sonrakine pipe_in olarak geçer */
            static char chain_buf[PIPE_BUF_SIZE];
            chain_buf[0] = '\0';
            for (int pi = 0; pi < pn; pi++) {
                const char *in  = (pi > 0) ? chain_buf : NULL;
                int is_last     = (pi == pn-1);
                if (!is_last) {
                    static char next_buf[PIPE_BUF_SIZE];
                    run_segment(pipe_parts[pi], in, 1, next_buf, PIPE_BUF_SIZE);
                    kstrcpy(chain_buf, next_buf);
                } else {
                    run_segment(pipe_parts[pi], in, 0, NULL, 0);
                }
            }
        }
    }
}

void shell_exec(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    char buf[CMD_BUF_SIZE];
    int len = 0;
    while (*cmd && len < CMD_BUF_SIZE - 1) buf[len++] = *cmd++;
    buf[len] = '\0';
    execute(buf);
}

/* ------------------------------------------------------------------ */
/* Tab completion                                                      */
/* ------------------------------------------------------------------ */
static const char *all_cmds[] = {
    "help","uname","date","uptime","perf","net","web","video","free","lscpu","lsmem","dmesg","df","sync","ssh","sshd",
    "echo","ls","cat","mkdir","touch","rm","cp","mv","write","pwd","cd",
    "stat","chmod","env","ps","history","clear","reboot",
    "grep","wc","head","tail","find","hexdump","whoami","hostname","id",
    "tty","seq","sort","uniq","cut","tr","printf","pushd","popd","dirs",
    "test","alias","unalias","export","printenv","unset","yes","true","false","exit",
    NULL
};

static void tab_complete(char *buf, int *len) {
    /* Son kelimeyi bul */
    int start = *len;
    while (start > 0 && buf[start-1] != ' ') start--;
    const char *prefix = buf + start;
    int plen = *len - start;

    /* Eşleşen ilk komutu bul (sadece ilk kelime için komut tamamla) */
    if (start == 0) {
        const char *match = NULL; int matches = 0;
        for (int i = 0; all_cmds[i]; i++) {
            int cl = (int)kstrlen(all_cmds[i]);
            if (cl >= plen && kmemcmp((const uint8_t*)all_cmds[i], (const uint8_t*)prefix, (uint32_t)plen)==0) {
                match = all_cmds[i]; matches++;
            }
        }
        if (matches == 1) {
            /* Önce mevcut prefix'i sil */
            for (int i = 0; i < plen; i++) fb_putc('\b');
            /* Tamamla */
            const char *rest = match + plen;
            while (*rest && *len < CMD_BUF_SIZE-2) { buf[(*len)++] = *rest; fb_putc(*rest); rest++; }
            buf[*len] = '\0';
        } else if (matches > 1) {
            /* Tüm eşleşmeleri göster */
            fb_putc('\n');
            for (int i = 0; all_cmds[i]; i++) {
                int cl = (int)kstrlen(all_cmds[i]);
                if (cl >= plen && kmemcmp((const uint8_t*)all_cmds[i], (const uint8_t*)prefix, (uint32_t)plen)==0) {
                    fb_puts(all_cmds[i]); fb_putc(' ');
                }
            }
            fb_putc('\n');
        }
    } else {
        /* Dosya/dizin tamamlama */
        vfs_node_t *dir = cwd_node ? cwd_node : fs_root;
        const char *match = NULL; int matches = 0;
        for (int i = 0; i < dir->child_count; i++) {
            int nl = (int)kstrlen(dir->children[i]->name);
            if (nl >= plen && kmemcmp((const uint8_t*)dir->children[i]->name,
                                      (const uint8_t*)prefix, (uint32_t)plen)==0) {
                match = dir->children[i]->name; matches++;
            }
        }
        if (matches == 1) {
            for (int i = 0; i < plen; i++) fb_putc('\b');
            const char *rest = match + plen;
            while (*rest && *len < CMD_BUF_SIZE-2) { buf[(*len)++] = *rest; fb_putc(*rest); rest++; }
            buf[*len] = '\0';
        } else if (matches > 1) {
            fb_putc('\n');
            for (int i = 0; i < dir->child_count; i++) {
                int nl = (int)kstrlen(dir->children[i]->name);
                if (nl >= plen && kmemcmp((const uint8_t*)dir->children[i]->name,
                                          (const uint8_t*)prefix, (uint32_t)plen)==0) {
                    fb_puts(dir->children[i]->name); fb_putc(' ');
                }
            }
            fb_putc('\n');
        }
    }
}

/* ------------------------------------------------------------------ */
/* Satır okuma — sol/sağ/home/end/del/tab destekli                   */
/* ------------------------------------------------------------------ */
static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_len = 0;
static int  cmd_cur = 0; /* imleç pozisyonu */
static int text_cells(const char *s,int bytes) {
    int count=0; for(int i=0;i<bytes;i++) if(((uint8_t)s[i]&0xc0)!=0x80) count++; return count;
}
static int previous_char(int at) { if(at) at--; while(at&&((uint8_t)cmd_buf[at]&0xc0)==0x80) at--; return at; }
static int next_char(int at) { if(at<cmd_len) at++; while(at<cmd_len&&((uint8_t)cmd_buf[at]&0xc0)==0x80) at++; return at; }
static void redraw_tail(int blank) {
    for(int i=cmd_cur;i<cmd_len;i++) fb_putc(cmd_buf[i]);
    if(blank) fb_putc(' ');
    fb_text_move(-text_cells(cmd_buf+cmd_cur,cmd_len-cmd_cur)-blank);
}

static void line_clear_fb(void) {
    /* İmleci satır başına götür */
    fb_text_move(-text_cells(cmd_buf,cmd_cur));
    /* Tüm karakterleri sil */
    int count=text_cells(cmd_buf,cmd_len);
    for (int i = 0; i < count; i++) fb_putc(' ');
    fb_text_move(-count);
    cmd_len = 0; cmd_cur = 0;
}

static void line_set_fb(const char *s) {
    line_clear_fb();
    while (*s && cmd_len < CMD_BUF_SIZE-1) {
        cmd_buf[cmd_len++] = *s;
        fb_putc(*s++);
    }
    cmd_buf[cmd_len] = '\0';
    cmd_cur = cmd_len;
}

static void read_line(void) {
    cmd_len = 0; cmd_cur = 0; cmd_buf[0] = '\0';
    fb_text_cursor(1);
    char input[3]; int input_len=0,input_need=0;

    while (1) {
        ui_poll();
        fb_present();
        char c;
        const char *action = ui_take_command();
        if (action) { line_set_fb(action); c = '\n'; }
        else {
            if (!kbd_poll()) { __asm__ volatile("pause"); continue; }
            c = kbd_getc();
            if (ui_handle_key(c)) continue;
        }

        /* Geçmiş */
        if (c == KBD_UP) {
            if (hist_count > 0 && hist_idx > 0) { hist_idx--; line_set_fb(history[hist_idx%HISTORY_SIZE]); }
            continue;
        }
        if (c == KBD_DOWN) {
            if (hist_idx < hist_count) {
                hist_idx++;
                if (hist_idx == hist_count) line_clear_fb();
                else line_set_fb(history[hist_idx%HISTORY_SIZE]);
            }
            continue;
        }

        /* İmleç hareketi */
        if (c == KBD_LEFT) {
            if (cmd_cur > 0) { cmd_cur=previous_char(cmd_cur); fb_text_move(-1); }
            continue;
        }
        if (c == KBD_RIGHT) {
            if (cmd_cur < cmd_len) { cmd_cur=next_char(cmd_cur); fb_text_move(1); }
            continue;
        }
        if (c == KBD_HOME) {
            fb_text_move(-text_cells(cmd_buf,cmd_cur)); cmd_cur=0;
            continue;
        }
        if (c == KBD_END) {
            fb_text_move(text_cells(cmd_buf+cmd_cur,cmd_len-cmd_cur)); cmd_cur=cmd_len;
            continue;
        }

        /* Delete tuşu: imleç sağındaki karakteri sil */
        if (c == KBD_DEL) {
            if (cmd_cur < cmd_len) {
                int bytes=next_char(cmd_cur)-cmd_cur;
                for (int i = cmd_cur; i < cmd_len-bytes; i++) cmd_buf[i] = cmd_buf[i+bytes];
                cmd_len-=bytes;
                cmd_buf[cmd_len] = '\0';
                /* İmlecin sağını yeniden çiz */
                redraw_tail(1);
            }
            continue;
        }

        /* Enter */
        if (c == '\n') {
            cmd_buf[cmd_len] = '\0';
            fb_text_cursor(0);
            /* İmleci satır sonuna götür */
            while (cmd_cur < cmd_len) { fb_putc(cmd_buf[cmd_cur]); cmd_cur++; }
            fb_putc('\n');
            return;
        }

        /* Backspace: imleç solundaki karakteri sil */
        if (c == '\b') {
            if (cmd_cur > 0) {
                int before=previous_char(cmd_cur),bytes=cmd_cur-before; cmd_cur=before;
                for (int i = cmd_cur; i < cmd_len-bytes; i++) cmd_buf[i] = cmd_buf[i+bytes];
                cmd_len-=bytes;
                cmd_buf[cmd_len] = '\0';
                fb_text_move(-1); redraw_tail(1);
            }
            continue;
        }

        /* Tab: completion */
        if (c == '\t') {
            tab_complete(cmd_buf, &cmd_len);
            cmd_cur = cmd_len;
            continue;
        }

        /* PageUp/PageDown: yoksay */
        if (c == KBD_PGUP || c == KBD_PGDN) continue;

        /* Normal karakter: imleç pozisyonuna ekle */
        unsigned byte=(uint8_t)c;
        if(byte>=0xc2&&byte<=0xef) { input[0]=c; input_len=1; input_need=byte<0xe0?2:3; continue; }
        if((byte&0xc0)==0x80) {
            if(!input_need) continue;
            input[input_len++]=c; if(input_len<input_need) continue;
        } else { input_need=0; input_len=1; input[0]=c; }
        if (byte>=0x20&&byte!=0x7f&&cmd_len+input_len<CMD_BUF_SIZE) {
            /* Sağa kaydır */
            for (int i = cmd_len-1; i >= cmd_cur; i--) cmd_buf[i+input_len] = cmd_buf[i];
            for(int i=0;i<input_len;i++) cmd_buf[cmd_cur+i]=input[i];
            cmd_len+=input_len;
            cmd_buf[cmd_len] = '\0';
            /* İmlecin konumundan sona kadar yeniden çiz */
            for (int i = cmd_cur; i < cmd_len; i++) fb_putc(cmd_buf[i]);
            /* İmleci doğru konuma geri al */
            fb_text_move(-text_cells(cmd_buf+cmd_cur+input_len,cmd_len-cmd_cur-input_len));
            cmd_cur+=input_len;
        }
        input_len=input_need=0;
    }
}

/* ------------------------------------------------------------------ */
/* Ana shell döngüsü                                                  */
/* ------------------------------------------------------------------ */
void shell_main(void) {
    cwd_node = fs_root;
    fb_puts_color("AXIS OS", 0x2563C5);
    fb_puts(" / an independent workspace\n");
    fb_puts_color("Version " OS_VERSION "  |  x86_64  |  Turkce Q\n\n", 0x92a8c4);
    fb_puts("Your workspace is ready.\n");
    fb_puts("Type 'help' to explore. Press the Windows key for Start.\n\n");

    while (1) {
        print_prompt();
        hist_idx = hist_count;
        read_line();
        if (cmd_buf[0]) {
            hist_add(cmd_buf);
            execute(cmd_buf);
        }
    }
}

void shell_on_key(uint8_t sc) { (void)sc; }
