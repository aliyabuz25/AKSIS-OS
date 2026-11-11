#include "posix.h"
#include "fs.h"
#include "process.h"
#include "clock.h"
#include "kernel_info.h"
#include "fb.h"

#define POSIX_FD_MAX 64
typedef struct { vfs_node_t *node; uint32_t offset; int flags; int used; } posix_fd_t;
static posix_fd_t fds[POSIX_FD_MAX];
static int posix_pid = 1;
static char posix_cwd[128] = "/";
static void posix_strncpy(char *dst, const char *src, size_t cap) {
    if(!cap)return; size_t i=0; while(src[i]&&i+1<cap){dst[i]=src[i];i++;} dst[i]=0;
}

void posix_init(void) {
    kmemset(fds, 0, sizeof(fds));
    posix_cwd[0]='/'; posix_cwd[1]=0;
    /* stdio is deliberately represented as reserved descriptors. */
    fds[0].used = fds[1].used = fds[2].used = 1;
}

static int alloc_fd(void) {
    for (int i=3;i<POSIX_FD_MAX;i++) if (!fds[i].used) { fds[i].used=1; return i; }
    return -1;
}

int posix_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path || path[0] != '/') return -1;
    vfs_node_t *node = fs_lookup(path);
    if (!node && (flags & POSIX_O_CREAT)) node = fs_touch_path(path, PERM_FILE_DEFAULT);
    if (!node || node->type != NODE_FILE) return -1;
    int fd=alloc_fd(); if (fd<0) return -1;
    if ((flags & POSIX_O_TRUNC) && (flags & 3) != POSIX_O_RDONLY) fs_write(node, (const uint8_t *)"", 0);
    fds[fd].node=node; fds[fd].offset=0; fds[fd].flags=flags; return fd;
}

int posix_close(int fd) {
    if (fd<3 || fd>=POSIX_FD_MAX || !fds[fd].used) return -1;
    fds[fd].used=0; fds[fd].node=NULL; return 0;
}

int posix_dup(int fd) {
    if (fd<0 || fd>=POSIX_FD_MAX || !fds[fd].used) return -1;
    int out=alloc_fd(); if(out<0) return -1;
    fds[out]=fds[fd]; return out;
}

int64_t posix_lseek(int fd, int64_t offset, int whence) {
    if(fd<3||fd>=POSIX_FD_MAX||!fds[fd].used||!fds[fd].node) return -1;
    int64_t base=0; if(whence==POSIX_SEEK_CUR) base=fds[fd].offset;
    else if(whence==POSIX_SEEK_END) base=fds[fd].node->size;
    int64_t next=base+offset; if(next<0) return -1;
    fds[fd].offset=(uint32_t)next; return next;
}

ssize_t posix_read(int fd, void *buf, size_t count) {
    if (fd<3 || fd>=POSIX_FD_MAX || !fds[fd].used || !fds[fd].node || !buf) return -1;
    vfs_node_t *n=fds[fd].node; if (fds[fd].offset >= n->size) return 0;
    size_t left=n->size-fds[fd].offset; if (count>left) count=left;
    kmemcpy(buf, fs_node_data(n)+fds[fd].offset, count); fds[fd].offset += (uint32_t)count;
    return (ssize_t)count;
}

ssize_t posix_write(int fd, const void *buf, size_t count) {
    if (fd==POSIX_STDOUT_FILENO || fd==POSIX_STDERR_FILENO) {
        if (buf) { char tmp[256]; size_t n=count<255?count:255; kmemcpy(tmp,buf,n); tmp[n]=0; fb_puts(tmp); }
        return (ssize_t)count;
    }
    if (fd<3 || fd>=POSIX_FD_MAX || !fds[fd].used || !fds[fd].node || !buf) return -1;
    return fs_write(fds[fd].node, (const uint8_t *)buf, (uint32_t)count);
}

static void fill_stat(vfs_node_t *n, posix_stat_t *st) {
    kmemset(st,0,sizeof(*st)); st->mode=n->permissions; st->size=n->size;
    st->uid=n->uid; st->gid=n->gid; st->type=(uint8_t)n->type;
}
int posix_fstat(int fd, posix_stat_t *st) { if(fd<3||fd>=POSIX_FD_MAX||!fds[fd].used||!fds[fd].node||!st)return -1; fill_stat(fds[fd].node,st); return 0; }
int posix_stat(const char *path, posix_stat_t *st) { vfs_node_t *n=fs_lookup(path); if(!n||!st)return -1; fill_stat(n,st); return 0; }
int posix_getpid(void) { return posix_pid; }
int posix_nanosleep(uint64_t milliseconds) { uint64_t end=get_uptime_ms()+milliseconds; while(get_uptime_ms()<end) __asm__ volatile("pause"); return 0; }
int posix_chdir(const char *path) { vfs_node_t *n=fs_lookup(path); if(!n||n->type!=NODE_DIR)return -1; posix_strncpy(posix_cwd,path,sizeof(posix_cwd)); return 0; }
int posix_getcwd(char *buf, size_t size) { size_t n=kstrlen(posix_cwd); if(!buf||size<=n)return -1; kstrcpy(buf,posix_cwd); return 0; }
int posix_mkdir(const char *path, int mode) {
    (void)mode; if(!path||path[0]!='/')return -1;
    if(fs_lookup(path))return -1;
    const char *slash=NULL; for(const char *p=path;*p;p++)if(*p=='/')slash=p;
    if(!slash||slash==path)return -1;
    char parent[128]; int n=(int)(slash-path); if(n>=(int)sizeof(parent))return -1;
    kmemcpy(parent,path,n); parent[n]=0; vfs_node_t *p=fs_lookup(parent); if(!p)return -1;
    char name[64]; kstrcpy(name,slash+1); return fs_mkdir(p,name,PERM_DIR_DEFAULT)?0:-1;
}
int posix_unlink(const char *path) { (void)path; return -1; /* VFS deletion is intentionally not destructive yet. */ }
int posix_isatty(int fd) { return fd==0||fd==1||fd==2; }
int64_t posix_sysconf(int name) { if(name==1)return 4096; if(name==2)return 1; if(name==3)return 64; return -1; }
void *posix_mmap(size_t length) { return kmalloc(length); }
int posix_munmap(void *address, size_t length) { (void)address; (void)length; return 0; }

int64_t posix_syscall(uint64_t number, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    switch(number) {
    case POSIX_SYS_READ: return posix_read((int)a0,(void *)a1,(size_t)a2);
    case POSIX_SYS_WRITE: return posix_write((int)a0,(const void *)a1,(size_t)a2);
    case POSIX_SYS_OPEN: return posix_open((const char *)a0,(int)a1,(int)a2);
    case POSIX_SYS_CLOSE: return posix_close((int)a0);
    case POSIX_SYS_STAT: return posix_stat((const char *)a0,(posix_stat_t *)a1);
    case POSIX_SYS_MMAP: return (int64_t)(uintptr_t)posix_mmap((size_t)a0);
    case POSIX_SYS_MUNMAP: return posix_munmap((void *)a0,(size_t)a1);
    case POSIX_SYS_GETPID: return posix_getpid();
    case POSIX_SYS_NANOSLEEP: return posix_nanosleep(a0);
    default: (void)a3; return -1;
    }
}
