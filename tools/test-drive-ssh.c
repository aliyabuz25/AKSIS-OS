/*
 * Test Suite: SHENLONG OS Persistent Drive (SHENFS02) & SSH Daemon (Port 22)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Mock kernel environment */
#define MAX_NAME        64
#define MAX_CHILDREN    32
#define MAX_NODES       256
#define MAX_DATA        4096

typedef enum {
    NODE_DIR  = 1,
    NODE_FILE = 2,
    NODE_LINK = 3,
    NODE_DEV  = 4,
} node_type_t;

typedef struct vfs_node {
    char            name[MAX_NAME];
    node_type_t     type;
    uint16_t        permissions;
    uint32_t        uid;
    uint32_t        gid;
    uint32_t        size;
    uint8_t         data[MAX_DATA];
    struct vfs_node *parent;
    struct vfs_node *children[MAX_CHILDREN];
    int              child_count;
    int              is_tmp;
} vfs_node_t;

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

/* Simulated 64MB IDE Disk */
#define DISK_SIZE_BYTES (64 * 1024 * 1024)
static uint8_t g_sim_disk[DISK_SIZE_BYTES];

int ata_drive_present(void) { return 1; }
uint32_t ata_sector_count(void) { return DISK_SIZE_BYTES / 512; }

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if ((lba + count) * 512 > DISK_SIZE_BYTES) return 0;
    memcpy(buf, g_sim_disk + (lba * 512), count * 512);
    return 1;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if ((lba + count) * 512 > DISK_SIZE_BYTES) return 0;
    memcpy(g_sim_disk + (lba * 512), buf, count * 512);
    return 1;
}

/* VFS State */
static vfs_node_t node_pool[MAX_NODES];
static int        node_pool_idx = 0;
vfs_node_t       *fs_root = NULL;
static int        fs_persistent = 0;

static vfs_node_t *alloc_node(void) {
    assert(node_pool_idx < MAX_NODES);
    vfs_node_t *n = &node_pool[node_pool_idx++];
    memset(n, 0, sizeof(vfs_node_t));
    return n;
}

vfs_node_t *fs_mkdir(vfs_node_t *parent, const char *name, uint16_t perms) {
    vfs_node_t *n = alloc_node();
    strncpy(n->name, name, MAX_NAME - 1);
    n->type        = NODE_DIR;
    n->permissions = perms;
    n->parent      = parent;
    if (parent && parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = n;
    }
    return n;
}

vfs_node_t *fs_mkfile(vfs_node_t *parent, const char *name, uint16_t perms) {
    vfs_node_t *n = alloc_node();
    strncpy(n->name, name, MAX_NAME - 1);
    n->type        = NODE_FILE;
    n->permissions = perms;
    n->parent      = parent;
    if (parent && parent->child_count < MAX_CHILDREN) {
        parent->children[parent->child_count++] = n;
    }
    return n;
}

vfs_node_t *fs_lookup(const char *path) {
    if (!path || path[0] != '/') return NULL;
    if (path[1] == '\0') return fs_root;

    vfs_node_t *cur = fs_root;
    char part[MAX_NAME];
    int pi = 0, i = 1;

    while (1) {
        char c = path[i];
        if (c == '/' || c == '\0') {
            part[pi] = '\0';
            if (pi > 0) {
                int found = 0;
                for (int j = 0; j < cur->child_count; j++) {
                    if (strcmp(cur->children[j]->name, part) == 0) {
                        cur = cur->children[j];
                        found = 1;
                        break;
                    }
                }
                if (!found) return NULL;
                pi = 0;
            }
            if (c == '\0') break;
        } else {
            if (pi < MAX_NAME - 1) part[pi++] = c;
        }
        i++;
    }
    return cur;
}

int fs_write(vfs_node_t *node, const uint8_t *data, uint32_t len) {
    if (!node || node->type != NODE_FILE) return -1;
    if (len > MAX_DATA) len = MAX_DATA;
    memcpy(node->data, data, len);
    node->size = len;
    return (int)len;
}

void fs_init(void) {
    node_pool_idx = 0;
    fs_root = alloc_node();
    strcpy(fs_root->name, "/");
    fs_root->type        = NODE_DIR;
    fs_root->permissions = 0755;
    fs_root->parent      = fs_root;
}

int fs_sync(void) {
    if (!ata_drive_present()) return 0;
    shenfs_super_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic         = SHENFS_MAGIC;
    sb.version       = 2;
    sb.node_count    = (uint32_t)node_pool_idx;
    sb.total_sectors = ata_sector_count();
    strcpy(sb.label, "SHENLONG_SYS");
    uint8_t sec_buf[512];
    memset(sec_buf, 0, 512);
    memcpy(sec_buf, &sb, sizeof(sb));
    ata_write_sectors(SHENFS_SB_LBA, 1, sec_buf);

    static shenfs_disk_node_t disk_nodes[MAX_NODES];
    memset(disk_nodes, 0, sizeof(disk_nodes));

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
        strncpy(disk_nodes[i].name, n->name, MAX_NAME - 1);

        if (n->type == NODE_FILE && n->size > 0) {
            uint8_t count = (uint8_t)((n->size + 511) / 512);
            if (count > SHENFS_SECTORS_PER_NODE) count = SHENFS_SECTORS_PER_NODE;
            ata_write_sectors(SHENFS_DATA_LBA + i * SHENFS_SECTORS_PER_NODE, count, n->data);
        }
    }
    uint8_t sectors = (uint8_t)((sizeof(disk_nodes) + 511) / 512);
    ata_write_sectors(SHENFS_NODE_LBA, sectors, disk_nodes);
    return 1;
}

void fs_setup_directories(void) {
    vfs_node_t *d_etc  = fs_mkdir(fs_root, "etc", 0755);
    vfs_node_t *d_home = fs_mkdir(fs_root, "home", 0755);
    vfs_node_t *f_host = fs_mkfile(d_etc, "hostname", 0644);
    fs_write(f_host, (const uint8_t *)"shenlong\n", 9);
    vfs_node_t *f_pwd  = fs_mkfile(d_etc, "passwd", 0644);
    const char *pwd = "root:x:0:0:root:/home/root:/bash/shell\n";
    fs_write(f_pwd, (const uint8_t *)pwd, (uint32_t)strlen(pwd));
    vfs_node_t *d_ssh  = fs_mkdir(d_etc, "ssh", 0755);
    vfs_node_t *f_sshd = fs_mkfile(d_ssh, "sshd_config", 0644);
    const char *cfg = "Port 22\nPermitRootLogin yes\n";
    fs_write(f_sshd, (const uint8_t *)cfg, (uint32_t)strlen(cfg));
    vfs_node_t *d_root = fs_mkdir(d_home, "root", 0755);
    vfs_node_t *f_wel  = fs_mkfile(d_root, "welcome.txt", 0644);
    const char *w = "SHENLONG OS v0.7.0 Kalici Disk\n";
    fs_write(f_wel, (const uint8_t *)w, (uint32_t)strlen(w));
}

int fs_mount_disk(void) {
    shenfs_super_t sb;
    uint8_t sec_buf[512];
    memset(sec_buf, 0, 512);
    if (!ata_read_sectors(SHENFS_SB_LBA, 1, sec_buf)) {
        fs_setup_directories();
        fs_persistent = 1;
        fs_sync();
        return 2;
    }
    memcpy(&sb, sec_buf, sizeof(sb));
    if (sb.magic != SHENFS_MAGIC) {
        fs_setup_directories();
        fs_persistent = 1;
        fs_sync();
        return 2;
    }
    uint32_t count = sb.node_count;
    if (count > MAX_NODES) count = MAX_NODES;

    static shenfs_disk_node_t disk_nodes[MAX_NODES];
    uint8_t sectors = (uint8_t)((sizeof(disk_nodes) + 511) / 512);
    if (!ata_read_sectors(SHENFS_NODE_LBA, sectors, disk_nodes)) {
        fs_setup_directories();
        return 0;
    }
    node_pool_idx = (int)count;
    memset(node_pool, 0, sizeof(node_pool));

    for (uint32_t i = 0; i < count; i++) {
        if (!disk_nodes[i].in_use) continue;
        vfs_node_t *n = &node_pool[i];
        strncpy(n->name, disk_nodes[i].name, MAX_NAME - 1);
        n->type        = (node_type_t)disk_nodes[i].type;
        n->permissions = disk_nodes[i].permissions;
        n->uid         = disk_nodes[i].uid;
        n->gid         = disk_nodes[i].gid;
        n->size        = disk_nodes[i].size;
    }
    fs_root = &node_pool[0];
    fs_root->parent = fs_root;

    for (uint32_t i = 1; i < count; i++) {
        if (!disk_nodes[i].in_use) continue;
        vfs_node_t *n = &node_pool[i];
        uint32_t pidx = disk_nodes[i].parent_idx;
        vfs_node_t *parent = (pidx < count) ? &node_pool[pidx] : fs_root;
        n->parent = parent;
        if (parent->child_count < MAX_CHILDREN) {
            parent->children[parent->child_count++] = n;
        }
    }

    for (uint32_t i = 0; i < count; i++) {
        vfs_node_t *n = &node_pool[i];
        if (n->type == NODE_FILE && n->size > 0) {
            uint8_t scnt = (uint8_t)((n->size + 511) / 512);
            if (scnt > SHENFS_SECTORS_PER_NODE) scnt = SHENFS_SECTORS_PER_NODE;
            ata_read_sectors(SHENFS_DATA_LBA + i * SHENFS_SECTORS_PER_NODE, scnt, n->data);
        }
    }
    fs_persistent = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Main Test Runner                                                   */
/* ------------------------------------------------------------------ */
int main(void) {
    printf("[*] Testing SHENLONG Persistent Drive (SHENFS02)...\n");

    /* 1. Mount fresh/unformatted disk */
    memset(g_sim_disk, 0, sizeof(g_sim_disk));
    fs_init();
    int mres = fs_mount_disk();
    assert(mres == 2); /* Formatted and mounted */
    assert(fs_lookup("/etc/hostname") != NULL);
    assert(fs_lookup("/etc/passwd") != NULL);
    assert(fs_lookup("/etc/ssh/sshd_config") != NULL);
    assert(fs_lookup("/home/root/welcome.txt") != NULL);
    printf("    [+] Initial format and VFS structure OK.\n");

    /* 2. Write a user file to persistent drive */
    vfs_node_t *root_dir = fs_lookup("/home/root");
    assert(root_dir != NULL);
    vfs_node_t *secret_file = fs_mkfile(root_dir, "secret_key.pem", 0600);
    const char *secret_content = "-----BEGIN SHENLONG HOST KEY-----\nABC123XYZ999\n-----END KEY-----\n";
    fs_write(secret_file, (const uint8_t *)secret_content, (uint32_t)strlen(secret_content));
    fs_sync();
    printf("    [+] Created /home/root/secret_key.pem and synced to drive.\n");

    /* 3. SIMULATE REBOOT (Wipe RAM node pool and remount from disk) */
    printf("[*] Simulating full OS reboot (wiping RAM nodes)...\n");
    memset(node_pool, 0, sizeof(node_pool));
    node_pool_idx = 0;
    fs_root = NULL;

    fs_init();
    mres = fs_mount_disk();
    assert(mres == 1); /* Mounted existing persistent disk */
    printf("    [+] Detected SHENFS02 Superblock and reloaded VFS tree from disk!\n");

    /* 4. Verify persisted user file */
    vfs_node_t *recovered = fs_lookup("/home/root/secret_key.pem");
    assert(recovered != NULL);
    assert(recovered->size == strlen(secret_content));
    assert(memcmp(recovered->data, secret_content, recovered->size) == 0);
    printf("    [+] Verified file content persisted 100%% across reboot: %u bytes.\n", recovered->size);

    /* 5. Verify /etc/passwd persisted */
    vfs_node_t *rec_pwd = fs_lookup("/etc/passwd");
    assert(rec_pwd != NULL);
    assert(strstr((const char *)rec_pwd->data, "root:x:0:0") != NULL);
    printf("    [+] Verified /etc/passwd persisted: %s", (const char *)rec_pwd->data);

    /* 6. Verify /etc/ssh/sshd_config persisted */
    vfs_node_t *rec_ssh = fs_lookup("/etc/ssh/sshd_config");
    assert(rec_ssh != NULL);
    assert(strstr((const char *)rec_ssh->data, "Port 22") != NULL);
    printf("    [+] Verified /etc/ssh/sshd_config persisted: %s", (const char *)rec_ssh->data);

    printf("\n>>> ALL DRIVE PERSISTENCE & SSH TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
