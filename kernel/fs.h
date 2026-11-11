/*
 * SHENLONG OS - Dosya Sistemi & İzin Sistemi Header
 */

#ifndef FS_H
#define FS_H

#include "kernel.h"

/* ------------------------------------------------------------------ */
/* İzin bitleri (Unix-benzeri)                                        */
/* ------------------------------------------------------------------ */
#define PERM_OWNER_R    0x0100  /* owner okuma   */
#define PERM_OWNER_W    0x0080  /* owner yazma   */
#define PERM_OWNER_X    0x0040  /* owner çalıştırma */
#define PERM_GROUP_R    0x0020
#define PERM_GROUP_W    0x0010
#define PERM_GROUP_X    0x0008
#define PERM_OTHER_R    0x0004
#define PERM_OTHER_W    0x0002
#define PERM_OTHER_X    0x0001

#define PERM_DIR_DEFAULT    (PERM_OWNER_R | PERM_OWNER_W | PERM_OWNER_X | \
                             PERM_GROUP_R | PERM_GROUP_X | \
                             PERM_OTHER_R | PERM_OTHER_X)   /* 755 */

#define PERM_FILE_DEFAULT   (PERM_OWNER_R | PERM_OWNER_W | \
                             PERM_GROUP_R | \
                             PERM_OTHER_R)                  /* 644 */

#define PERM_EXEC_DEFAULT   (PERM_OWNER_R | PERM_OWNER_W | PERM_OWNER_X | \
                             PERM_GROUP_R | PERM_GROUP_X | \
                             PERM_OTHER_R | PERM_OTHER_X)   /* 755 */

#define PERM_TMP            (PERM_OWNER_R | PERM_OWNER_W | PERM_OWNER_X | \
                             PERM_GROUP_R | PERM_GROUP_W | PERM_GROUP_X | \
                             PERM_OTHER_R | PERM_OTHER_W | PERM_OTHER_X) /* 777 */

/* ------------------------------------------------------------------ */
/* Düğüm tipleri                                                      */
/* ------------------------------------------------------------------ */
typedef enum {
    NODE_DIR  = 1,
    NODE_FILE = 2,
    NODE_LINK = 3,
    NODE_DEV  = 4,
} node_type_t;

/* ------------------------------------------------------------------ */
/* VFS düğüm yapısı                                                   */
/* ------------------------------------------------------------------ */
#define MAX_NAME        64
#define MAX_CHILDREN    32
#define MAX_NODES       256
#define MAX_DATA        4096

typedef struct vfs_node {
    char            name[MAX_NAME];
    node_type_t     type;
    uint16_t        permissions;
    uint32_t        uid;            /* owner user id  */
    uint32_t        gid;            /* owner group id */
    uint32_t        size;
    uint8_t         data[MAX_DATA]; /* dosya içeriği (küçük dosyalar/disk) */
    uint8_t        *dyn_data;       /* dinamik büyük dosya içeriği (video vb.) */
    uint32_t        dyn_cap;        /* dinamik tampon boyutu */

    struct vfs_node *parent;
    struct vfs_node *children[MAX_CHILDREN];
    int              child_count;

    /* tmpfs için: bellek tabanlı bayrak */
    int              is_tmp;
} vfs_node_t;

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */
void            fs_init(void);
void            fs_setup_directories(void);
void            tmpfs_mount(const char *path);
void            perm_init(void);
vfs_node_t     *fs_mkdir(vfs_node_t *parent, const char *name, uint16_t perms);
vfs_node_t     *fs_mkfile(vfs_node_t *parent, const char *name, uint16_t perms);
vfs_node_t     *fs_lookup(const char *path);
vfs_node_t     *fs_touch_path(const char *path, uint16_t perms);
int             fs_write(vfs_node_t *node, const uint8_t *data, uint32_t len);
int             fs_read(vfs_node_t *node, uint8_t *buf, uint32_t len);
const uint8_t  *fs_node_data(const vfs_node_t *node);
int             fs_mount_disk(void);
int             fs_sync(void);
int             fs_format(void);

vfs_node_t *fs_mkdir(vfs_node_t *parent, const char *name, uint16_t perms);
vfs_node_t *fs_mkfile(vfs_node_t *parent, const char *name, uint16_t perms);
vfs_node_t *fs_lookup(const char *path);
int         fs_write(vfs_node_t *node, const uint8_t *data, uint32_t len);
int         fs_read(vfs_node_t *node, uint8_t *buf, uint32_t len);

int         perm_check(vfs_node_t *node, uint32_t uid, int want_read,
                       int want_write, int want_exec);

int         fs_mount_disk(void);
int         fs_sync(void);
int         fs_is_persistent(void);

/* Kök düğümü dışarıya aç */
extern vfs_node_t *fs_root;

#endif /* FS_H */