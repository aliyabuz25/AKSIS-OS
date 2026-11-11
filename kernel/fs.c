/*
 * SHENLONG OS - Dosya Sistemi & İzin Sistemi
 * Bellek tabanlı VFS + ATA/IDE Disk Kalıcılığı (SHENFS02)
 */

#include "fs.h"
#include "ata.h"
#include "kernel.h"
#include "html_ui.h"

#define SHENFS_MAGIC            0x5348454E46533032ULL /* "SHENFS02" */
#define SHENFS_SB_LBA           2
#define SHENFS_NODE_LBA         4
#define SHENFS_NODE_SECTORS     44
#define SHENFS_DATA_LBA         64
#define SHENFS_SECTORS_PER_NODE 8

typedef struct {
    uint32_t in_use;
    uint32_t parent_idx;
    uint16_t type;
    uint16_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    char     name[MAX_NAME];
} __attribute__((packed)) shenfs_disk_node_t;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t node_count;
    uint32_t total_sectors;
    char     label[32];
} __attribute__((packed)) shenfs_super_t;

static int fs_persistent = 0;

/* ------------------------------------------------------------------ */
/* Global düğüm havuzu                                                */
/* ------------------------------------------------------------------ */
static vfs_node_t node_pool[MAX_NODES];
static int        node_pool_idx = 0;

vfs_node_t *fs_root = NULL;

/* ------------------------------------------------------------------ */
/* Yardımcı: havuzdan düğüm al                                        */
/* ------------------------------------------------------------------ */
static vfs_node_t *alloc_node(void) {
    if (node_pool_idx >= MAX_NODES) {
        kernel_panic("fs: node havuzu doldu");
    }
    vfs_node_t *n = &node_pool[node_pool_idx++];
    kmemset(n, 0, sizeof(vfs_node_t));
    return n;
}

/* ------------------------------------------------------------------ */
/* fs_mkdir                                                           */
/* ------------------------------------------------------------------ */
vfs_node_t *fs_mkdir(vfs_node_t *parent, const char *name, uint16_t perms) {
    vfs_node_t *n = alloc_node();
    kstrcpy(n->name, name);
    n->type        = NODE_DIR;
    n->permissions = perms;
    n->uid         = 0;
    n->gid         = 0;
    n->parent      = parent;

    if (parent && parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = n;
    }
    if (fs_persistent) {
        fs_sync();
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* fs_mkfile                                                          */
/* ------------------------------------------------------------------ */
vfs_node_t *fs_mkfile(vfs_node_t *parent, const char *name, uint16_t perms) {
    vfs_node_t *n = alloc_node();
    kstrcpy(n->name, name);
    n->type        = NODE_FILE;
    n->permissions = perms;
    n->uid         = 0;
    n->gid         = 0;
    n->parent      = parent;

    if (parent && parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = n;
    }
    if (fs_persistent) {
        fs_sync();
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* fs_lookup: "/bash/shell" gibi yolu çöz                             */
/* ------------------------------------------------------------------ */
vfs_node_t *fs_lookup(const char *path) {
    if (!path || path[0] != '/') return NULL;
    if (path[1] == '\0') return fs_root;

    vfs_node_t *cur = fs_root;
    char part[MAX_NAME];
    int  pi = 0;
    int  i  = 1; /* '/' atla */

    while (1) {
        char c = path[i];
        if (c == '/' || c == '\0') {
            part[pi] = '\0';
            if (pi == 0) { /* çift slash */
                if (c == '\0') break;
                i++;
                continue;
            }
            /* part içinde çocuk ara */
            int found = 0;
            for (int j = 0; j < cur->child_count; j++) {
                if (kstrcmp(cur->children[j]->name, part) == 0) {
                    cur   = cur->children[j];
                    found = 1;
                    break;
                }
            }
            if (!found) return NULL;
            pi = 0;
            if (c == '\0') break;
        } else {
            if (pi < MAX_NAME - 1) part[pi++] = c;
        }
        i++;
    }
    return cur;
}

/* ------------------------------------------------------------------ */
/* fs_write / fs_read / fs_node_data                                  */
/* ------------------------------------------------------------------ */
const uint8_t *fs_node_data(const vfs_node_t *node) {
    if (!node) return NULL;
    return node->dyn_data ? node->dyn_data : node->data;
}

vfs_node_t *fs_touch_path(const char *path, uint16_t perms) {
    if (!path || !path[0]) return NULL;
    vfs_node_t *exist = fs_lookup(path);
    if (exist) return exist;

    /* Son '/' konumunu bul */
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent = fs_root;
    const char *fname = path;

    if (last_slash) {
        fname = last_slash + 1;
        if (last_slash == path) {
            parent = fs_root;
        } else {
            char dir_path[MAX_NAME * 2];
            int dlen = (int)(last_slash - path);
            if (dlen >= (int)sizeof(dir_path)) dlen = sizeof(dir_path) - 1;
            kmemcpy(dir_path, path, dlen);
            dir_path[dlen] = '\0';
            parent = fs_lookup(dir_path);
            if (!parent) {
                /* Dizin yoksa oluşturmayı dene (ör. /tmp) */
                parent = fs_mkdir(fs_root, dir_path + (dir_path[0] == '/' ? 1 : 0), 0777);
            }
        }
    }

    if (!parent || !fname[0]) return NULL;
    return fs_mkfile(parent, fname, perms);
}

int fs_write(vfs_node_t *node, const uint8_t *data, uint32_t len) {
    if (!node || node->type != NODE_FILE) return -1;

    if (len <= MAX_DATA && !node->dyn_data) {
        kmemcpy(node->data, data, len);
        node->size = len;
    } else {
        if (!node->dyn_data || node->dyn_cap < len) {
            uint32_t new_cap = len < 65536 ? 65536 : len;
            node->dyn_data = (uint8_t *)kmalloc(new_cap);
            if (!node->dyn_data) return -1;
            node->dyn_cap = new_cap;
        }
        kmemcpy(node->dyn_data, data, len);
        node->size = len;
        uint32_t c = len < MAX_DATA ? len : MAX_DATA;
        kmemcpy(node->data, data, c);
    }

    if (fs_persistent && !node->is_tmp) {
        fs_sync();
    }
    return (int)len;
}

int fs_read(vfs_node_t *node, uint8_t *buf, uint32_t len) {
    if (!node || node->type != NODE_FILE) return -1;
    const uint8_t *src = fs_node_data(node);
    if (len > node->size) len = node->size;
    kmemcpy(buf, src, len);
    return (int)len;
}

/* ------------------------------------------------------------------ */
/* tmpfs_mount: dizini geçici (RAM) olarak işaretle                   */
/* ------------------------------------------------------------------ */
void tmpfs_mount(const char *path) {
    vfs_node_t *n = fs_lookup(path);
    if (n) {
        n->is_tmp      = 1;
        n->permissions = PERM_TMP; /* 777 */
    }
}

/* ------------------------------------------------------------------ */
/* perm_check                                                         */
/* ------------------------------------------------------------------ */
void perm_init(void) {
}

int perm_check(vfs_node_t *node, uint32_t uid,
               int want_read, int want_write, int want_exec) {
    if (!node) return 0;

    uint16_t p = node->permissions;
    uint16_t r_bit, w_bit, x_bit;

    if (uid == node->uid) {
        r_bit = PERM_OWNER_R;
        w_bit = PERM_OWNER_W;
        x_bit = PERM_OWNER_X;
    } else {
        r_bit = PERM_OTHER_R;
        w_bit = PERM_OTHER_W;
        x_bit = PERM_OTHER_X;
    }

    if (want_read  && !(p & r_bit)) return 0;
    if (want_write && !(p & w_bit)) return 0;
    if (want_exec  && !(p & x_bit)) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* fs_init: kök ve temel dizin ağacını oluştur                        */
/* ------------------------------------------------------------------ */
void fs_init(void) {
    node_pool_idx = 0;
    fs_root = alloc_node();
    kstrcpy(fs_root->name, "/");
    fs_root->type        = NODE_DIR;
    fs_root->permissions = PERM_DIR_DEFAULT;
    fs_root->uid         = 0;
    fs_root->gid         = 0;
    fs_root->parent      = fs_root; /* kök kendine işaret eder */
}

void fs_setup_directories(void) {
    /* Temel dizin yapısı */
    vfs_node_t *d_tmp  = fs_mkdir(fs_root, "tmp",  PERM_TMP);
    vfs_node_t *d_sys  = fs_mkdir(fs_root, "sys",  PERM_DIR_DEFAULT);
    vfs_node_t *d_bash = fs_mkdir(fs_root, "bash", PERM_DIR_DEFAULT);
    vfs_node_t *d_bin  = fs_mkdir(fs_root, "bin",  PERM_DIR_DEFAULT);
    vfs_node_t *d_dev  = fs_mkdir(fs_root, "dev",  PERM_DIR_DEFAULT);
    vfs_node_t *d_etc  = fs_mkdir(fs_root, "etc",  PERM_DIR_DEFAULT);
    vfs_node_t *d_home = fs_mkdir(fs_root, "home", PERM_DIR_DEFAULT);
    vfs_node_t *d_var  = fs_mkdir(fs_root, "var",  PERM_DIR_DEFAULT);
    vfs_node_t *d_movies = fs_mkdir(fs_root, "movies", PERM_DIR_DEFAULT);
    vfs_node_t *d_videos = fs_mkdir(fs_root, "videos", PERM_DIR_DEFAULT);

    (void)d_tmp; (void)d_dev; (void)d_var; (void)d_movies; (void)d_videos;

    /* /sys alt düğümleri */
    fs_mkfile(d_sys, "version",   PERM_FILE_DEFAULT);
    fs_mkfile(d_sys, "uptime",    PERM_FILE_DEFAULT);
    fs_mkfile(d_sys, "meminfo",   PERM_FILE_DEFAULT);

    /* /etc dosyaları */
    vfs_node_t *f_hostname = fs_mkfile(d_etc, "hostname", PERM_FILE_DEFAULT);
    fs_write(f_hostname, (const uint8_t *)"shenlong\n", 9);

    vfs_node_t *f_passwd = fs_mkfile(d_etc, "passwd", PERM_FILE_DEFAULT);
    const char *passwd_data = "root:x:0:0:root:/home/root:/bash/shell\n";
    fs_write(f_passwd, (const uint8_t *)passwd_data, (uint32_t)kstrlen(passwd_data));

    vfs_node_t *d_ssh = fs_mkdir(d_etc, "ssh", PERM_DIR_DEFAULT);
    vfs_node_t *f_sshd_cfg = fs_mkfile(d_ssh, "sshd_config", PERM_FILE_DEFAULT);
    const char *sshd_cfg = "Port 22\nPermitRootLogin yes\nSubsystem sftp internal-sftp\n";
    fs_write(f_sshd_cfg, (const uint8_t *)sshd_cfg, (uint32_t)kstrlen(sshd_cfg));

    /* /home/root */
    vfs_node_t *d_root = fs_mkdir(d_home, "root", PERM_DIR_DEFAULT);
    vfs_node_t *f_welcome = fs_mkfile(d_root, "welcome.txt", PERM_FILE_DEFAULT);
    const char *welcome = "AXIS OS v6.0.2\nKalıcı disk etkin (/dev/ide0, AXISFS02).\nOluşturulan veya düzenlenen tüm dosyalar diske kaydedilir.\n";
    fs_write(f_welcome, (const uint8_t *)welcome, (uint32_t)kstrlen(welcome));

    /* /bash çalıştırılabilir stub'lar */
    vfs_node_t *f_shell = fs_mkfile(d_bash, "shell", PERM_EXEC_DEFAULT);
    fs_write(f_shell, (const uint8_t *)"SHENLONG_SHELL_BINARY", 21);

    /* /bin araçları */
    vfs_node_t *f_echo = fs_mkfile(d_bin, "echo", PERM_EXEC_DEFAULT);
    vfs_node_t *f_ls   = fs_mkfile(d_bin, "ls",   PERM_EXEC_DEFAULT);
    vfs_node_t *f_cat  = fs_mkfile(d_bin, "cat",  PERM_EXEC_DEFAULT);

    fs_write(f_echo, (const uint8_t *)"SHENLONG_ECHO_BINARY", 20);
    fs_write(f_ls,   (const uint8_t *)"SHENLONG_LS_BINARY",   18);
    fs_write(f_cat,  (const uint8_t *)"SHENLONG_CAT_BINARY",  19);

    /* Current pages embedded from assets/ui at build time. */
    vfs_node_t *d_ui = fs_mkdir(fs_root, "ui", PERM_DIR_DEFAULT);
    if(d_ui) {
        vfs_node_t *f = fs_mkfile(d_ui, "bootstrap.css", PERM_FILE_DEFAULT);
        const char *data = html_ui_get_bootstrap_css();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "ui.css", PERM_FILE_DEFAULT);
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "index.html", PERM_FILE_DEFAULT);
        data = html_ui_get_welcome_page();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "system.html", PERM_FILE_DEFAULT);
        data = html_ui_get_system_page();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "network.html", PERM_FILE_DEFAULT);
        data = html_ui_get_network_page();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "about.html", PERM_FILE_DEFAULT);
        data = html_ui_get_about_page();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "files.html", PERM_FILE_DEFAULT);
        data = html_ui_get_files_page();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));

        f = fs_mkfile(d_ui, "bluetooth.html", PERM_FILE_DEFAULT);
        data = html_ui_get_bluetooth_page();
        fs_write(f, (const uint8_t *)data, (uint32_t)kstrlen(data));
    }
}

/* Refresh embedded app pages when an older persistent disk is mounted. */
static void fs_refresh_ui_pages(void) {
    vfs_node_t *d_ui = fs_lookup("/ui");
    if (!d_ui || d_ui->type != NODE_DIR) d_ui = fs_mkdir(fs_root, "ui", PERM_DIR_DEFAULT);
    if (!d_ui) return;
    const char *names[] = {"index.html", "system.html", "network.html", "about.html", "files.html", "bluetooth.html"};
    const char *data[] = {html_ui_get_welcome_page(), html_ui_get_system_page(), html_ui_get_network_page(),
                          html_ui_get_about_page(), html_ui_get_files_page(), html_ui_get_bluetooth_page()};
    for (int i = 0; i < 6; i++) {
        char path[64];
        kstrcpy(path, "/ui/"); kstrcat(path, names[i]);
        vfs_node_t *f = fs_lookup(path);
        if (!f) f = fs_mkfile(d_ui, names[i], PERM_FILE_DEFAULT);
        if (f) fs_write(f, (const uint8_t *)data[i], (uint32_t)kstrlen(data[i]));
    }
    vfs_node_t *css = fs_lookup("/ui/ui.css");
    if (!css) css = fs_mkfile(d_ui, "ui.css", PERM_FILE_DEFAULT);
    if (css) fs_write(css, (const uint8_t *)html_ui_get_bootstrap_css(), (uint32_t)kstrlen(html_ui_get_bootstrap_css()));
}

/* ------------------------------------------------------------------ */
/* Disk Kalıcılığı (SHENFS02)                                         */
/* ------------------------------------------------------------------ */
int fs_sync(void) {
    if (!ata_drive_present()) return 0;

    shenfs_super_t sb;
    kmemset(&sb, 0, sizeof(sb));
    sb.magic         = SHENFS_MAGIC;
    sb.version       = 2;
    sb.node_count    = (uint32_t)node_pool_idx;
    sb.total_sectors = ata_sector_count();
    kstrcpy(sb.label, "SHENLONG_SYS");

    /* Süperbloğu yaz (LBA 2) */
    uint8_t sec_buf[512];
    kmemset(sec_buf, 0, sizeof(sec_buf));
    kmemcpy(sec_buf, &sb, sizeof(sb));
    if (!ata_write_sectors(SHENFS_SB_LBA, 1, sec_buf)) return 0;

    /* Düğüm tablosunu hazırla */
    static shenfs_disk_node_t disk_nodes[MAX_NODES];
    kmemset(disk_nodes, 0, sizeof(disk_nodes));

    for (int i = 0; i < node_pool_idx; i++) {
        vfs_node_t *n = &node_pool[i];
        if (n->name[0] == '\0') continue;

        disk_nodes[i].in_use      = 1;
        disk_nodes[i].parent_idx  = (n->parent ? (uint32_t)(n->parent - node_pool) : 0);
        disk_nodes[i].type        = (uint16_t)n->type;
        disk_nodes[i].permissions = n->permissions;
        disk_nodes[i].uid         = n->uid;
        disk_nodes[i].gid         = n->gid;
        disk_nodes[i].size        = n->size;
        kstrcpy(disk_nodes[i].name, n->name);

        /* Dosya içeriğini diske yaz */
        if (n->type == NODE_FILE && n->size > 0 && !n->is_tmp) {
            uint8_t count = (uint8_t)((n->size + 511) / 512);
            if (count > SHENFS_SECTORS_PER_NODE) count = SHENFS_SECTORS_PER_NODE;
            ata_write_sectors(SHENFS_DATA_LBA + i * SHENFS_SECTORS_PER_NODE, count, n->data);
        }
    }

    uint8_t sectors = (uint8_t)((sizeof(disk_nodes) + 511) / 512);
    if (!ata_write_sectors(SHENFS_NODE_LBA, sectors, disk_nodes)) return 0;

    return 1;
}

int fs_mount_disk(void) {
    if (!ata_drive_present()) {
        fs_setup_directories();
        return 0;
    }

    shenfs_super_t sb;
    uint8_t sec_buf[512];
    kmemset(sec_buf, 0, sizeof(sec_buf));
    if (!ata_read_sectors(SHENFS_SB_LBA, 1, sec_buf)) {
        fs_setup_directories();
        return 0;
    }
    kmemcpy(&sb, sec_buf, sizeof(sb));
    if (sb.magic != SHENFS_MAGIC) {
        /* Disk boş veya henüz biçimlendirilmemiş — formatla ve ilk ağacı yaz */
        fs_setup_directories();
        fs_persistent = 1;
        fs_sync();
        return 2; /* Biçimlendirildi ve bağlandı */
    }

    /* Diskten mevcut dosya sistemi metaverilerini yükle */
    uint32_t count = sb.node_count;
    if (count > MAX_NODES) count = MAX_NODES;

    static shenfs_disk_node_t disk_nodes[MAX_NODES];
    uint8_t sectors = (uint8_t)((sizeof(disk_nodes) + 511) / 512);
    if (!ata_read_sectors(SHENFS_NODE_LBA, sectors, disk_nodes)) {
        fs_setup_directories();
        return 0;
    }

    node_pool_idx = (int)count;
    kmemset(node_pool, 0, sizeof(node_pool));

    /* 1. Aşama: Düğümleri yükle */
    for (uint32_t i = 0; i < count; i++) {
        if (!disk_nodes[i].in_use) continue;
        vfs_node_t *n = &node_pool[i];
        kstrcpy(n->name, disk_nodes[i].name);
        n->type        = (node_type_t)disk_nodes[i].type;
        n->permissions = disk_nodes[i].permissions;
        n->uid         = disk_nodes[i].uid;
        n->gid         = disk_nodes[i].gid;
        n->size        = disk_nodes[i].size;
    }

    /* 2. Aşama: Ebeveyn ve çocuk bağlantıları */
    fs_root = &node_pool[0];
    fs_root->parent = fs_root;

    for (uint32_t i = 1; i < count; i++) {
        if (!disk_nodes[i].in_use) continue;
        vfs_node_t *n = &node_pool[i];
        uint32_t pidx = disk_nodes[i].parent_idx;
        if (pidx < count) {
            vfs_node_t *parent = &node_pool[pidx];
            n->parent = parent;
            if (parent->child_count < MAX_CHILDREN) {
                parent->children[parent->child_count++] = n;
            }
        } else {
            n->parent = fs_root;
            if (fs_root->child_count < MAX_CHILDREN) {
                fs_root->children[fs_root->child_count++] = n;
            }
        }
    }

    /* 3. Aşama: Dosya verilerini diskten oku */
    for (uint32_t i = 0; i < count; i++) {
        vfs_node_t *n = &node_pool[i];
        if (n->type == NODE_FILE && n->size > 0) {
            uint8_t scnt = (uint8_t)((n->size + 511) / 512);
            if (scnt > SHENFS_SECTORS_PER_NODE) scnt = SHENFS_SECTORS_PER_NODE;
            ata_read_sectors(SHENFS_DATA_LBA + i * SHENFS_SECTORS_PER_NODE, scnt, n->data);
        }
    }

    fs_persistent = 1;
    fs_refresh_ui_pages();
    return 1;
}

int fs_is_persistent(void) {
    return fs_persistent && ata_drive_present();
}
