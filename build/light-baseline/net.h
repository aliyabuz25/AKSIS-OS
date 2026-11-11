#ifndef SHENLONG_NET_H
#define SHENLONG_NET_H
#include "kernel.h"

/* The bundled VM uses QEMU user networking (host Wi-Fi/Ethernet via NAT). */
#define NET_HTTP_CAP 98304
enum { NET_IDLE, NET_DNS, NET_CONNECT, NET_RECEIVE, NET_DONE, NET_ERROR };
typedef struct {
    int present, enabled, link, gateway;
    uint8_t mac[6];
    uint32_t ip, router, dns;
    uint64_t rx_packets, tx_packets, dropped;
} net_info_t;
extern net_info_t net_info;
void net_init(void);
void net_poll(void);
void net_enable(int enabled);
int net_http_start(const char *host, uint16_t port, const char *request);
void net_http_cancel(void);
int net_http_state(void);
const char *net_http_error(void);
const char *net_http_data(void);
unsigned net_http_size(void);
void net_ip_text(uint32_t ip, char *out);
#endif
