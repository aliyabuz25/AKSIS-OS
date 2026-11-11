/*
 * SHENLONG OS - Proses Yöneticisi Header
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "kernel.h"

#define MAX_PROCESSES   16
#define STACK_SIZE      4096

typedef enum {
    PROC_READY   = 0,
    PROC_RUNNING = 1,
    PROC_BLOCKED = 2,
    PROC_ZOMBIE  = 3,
} proc_state_t;

typedef struct process {
    int          pid;
    char         name[64];
    proc_state_t state;
    uint32_t     stack[STACK_SIZE / 4];
    uint32_t     esp;           /* stack pointer */
    uint32_t     eip;           /* instruction pointer */
    int          exit_code;
} process_t;

void  process_init(void);
int   process_exec(const char *path);
void  process_exit(int code);

extern process_t *current_process;

#endif /* PROCESS_H */