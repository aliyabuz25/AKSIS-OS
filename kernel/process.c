/*
 * SHENLONG OS - Proses Yöneticisi (Framebuffer)
 */

#include "process.h"
#include "kernel.h"
#include "fb.h"
#include "fs.h"

static process_t proc_table[MAX_PROCESSES];
static int       proc_count = 0;

process_t *current_process = NULL;

void process_init(void) {
    kmemset(proc_table, 0, sizeof(proc_table));
    proc_count = 0;
}

int process_exec(const char *path) {
    vfs_node_t *node = fs_lookup(path);
    if (!node) {
        fb_puts("[PROC] Dosya bulunamadi: ");
        fb_puts(path);
        fb_puts("\n");
        return -1;
    }

    if (!perm_check(node, 0, 0, 0, 1)) {
        fb_puts("[PROC] Calistirma izni yok: ");
        fb_puts(path);
        fb_puts("\n");
        return -1;
    }

    if (proc_count >= MAX_PROCESSES) {
        fb_puts("[PROC] Proses tablosu dolu!\n");
        return -1;
    }

    process_t *p = &proc_table[proc_count];
    p->pid       = proc_count + 1;
    p->state     = PROC_RUNNING;
    p->exit_code = 0;
    kstrcpy(p->name, path);
    proc_count++;
    current_process = p;

    extern void shell_main(void);
    shell_main();

    p->state = PROC_ZOMBIE;
    return 0;
}

void process_exit(int code) {
    if (current_process) {
        current_process->state     = PROC_ZOMBIE;
        current_process->exit_code = code;
    }
}