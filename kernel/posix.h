#ifndef SHENLONG_POSIX_H
#define SHENLONG_POSIX_H

#include "kernel.h"

/* Small POSIX-shaped ABI shared by the kernel and future user processes. */
#define POSIX_STDIN_FILENO  0
#define POSIX_STDOUT_FILENO 1
#define POSIX_STDERR_FILENO 2
#define POSIX_O_RDONLY 0
#define POSIX_O_WRONLY 1
#define POSIX_O_RDWR   2
#define POSIX_O_CREAT  0100
#define POSIX_O_TRUNC  01000
#define POSIX_O_APPEND 02000
#define POSIX_SEEK_SET 0
#define POSIX_SEEK_CUR 1
#define POSIX_SEEK_END 2

typedef struct {
    uint32_t mode;
    uint32_t size;
    uint32_t uid;
    uint32_t gid;
    uint64_t mtime;
    uint8_t  type;
} posix_stat_t;

void     posix_init(void);
int      posix_open(const char *path, int flags, int mode);
int      posix_close(int fd);
int      posix_dup(int fd);
int64_t  posix_lseek(int fd, int64_t offset, int whence);
ssize_t  posix_read(int fd, void *buf, size_t count);
ssize_t  posix_write(int fd, const void *buf, size_t count);
int      posix_fstat(int fd, posix_stat_t *st);
int      posix_stat(const char *path, posix_stat_t *st);
int      posix_getpid(void);
int      posix_nanosleep(uint64_t milliseconds);
int      posix_chdir(const char *path);
int      posix_getcwd(char *buf, size_t size);
int      posix_mkdir(const char *path, int mode);
int      posix_unlink(const char *path);
int      posix_isatty(int fd);
int64_t  posix_sysconf(int name);
void    *posix_mmap(size_t length);
int      posix_munmap(void *address, size_t length);

/* Reserved syscall numbers for a future ring-3 entry path. */
enum { POSIX_SYS_EXIT=1, POSIX_SYS_READ, POSIX_SYS_WRITE, POSIX_SYS_OPEN,
       POSIX_SYS_CLOSE, POSIX_SYS_STAT, POSIX_SYS_MMAP, POSIX_SYS_MUNMAP,
       POSIX_SYS_GETPID, POSIX_SYS_NANOSLEEP };

/* Kernel-side dispatcher used by the future ring-3 syscall entry. */
int64_t posix_syscall(uint64_t number, uint64_t a0, uint64_t a1,
                      uint64_t a2, uint64_t a3);

#endif
