#ifndef SHENLONG_SSH_H
#define SHENLONG_SSH_H

#include "kernel.h"

#define SSH_PORT 22

typedef struct {
    int      running;
    int      connected;
    uint32_t peer_ip;
    uint16_t peer_port;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t session_count;
    char     client_version[64];
    char     last_command[128];
} ssh_status_t;

extern ssh_status_t g_sshd_status;

void ssh_init(void);
void ssh_poll(void);
void ssh_server_receive(uint32_t src_ip, const uint8_t *tcp_payload, unsigned len);
void ssh_client_receive(const uint8_t *tcp_payload, unsigned len);
int  ssh_client_start(const char *host, uint16_t port, const char *user, const char *cmd);
void ssh_sshd_enable(int enable);
const ssh_status_t *ssh_get_status(void);

#endif /* SHENLONG_SSH_H */
