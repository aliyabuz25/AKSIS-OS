#ifndef SHENLONG_NET_H
#define SHENLONG_NET_H
#include "kernel.h"

/* The bundled VM uses QEMU user networking (host Wi-Fi/Ethernet via NAT). */
#define NET_HTTP_CAP (2u*1024u*1024u)
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
int net_web_start(const char *host,uint16_t port,const char *request,int secure);
int net_http_secure(void);
void net_http_cancel(void);
int net_http_state(void);
const char *net_http_error(void);
const char *net_http_data(void);
unsigned net_http_size(void);
void net_ip_text(uint32_t ip, char *out);
int net_send_tcp(uint32_t dest_ip, uint16_t src_port, uint16_t dst_port,
                 unsigned flags, uint32_t seq, uint32_t ack,
                 const void *data, unsigned size);
#endif
