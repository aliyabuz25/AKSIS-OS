#ifndef SHENLONG_TLS_H
#define SHENLONG_TLS_H
#include "kernel.h"
int tls_start(const char *host);
const char *tls_error(void);
unsigned tls_feed(const void *data,unsigned size);
const uint8_t *tls_send_record(unsigned *size);
void tls_sent_record(unsigned size);
unsigned tls_write(const void *data,unsigned size);
const uint8_t *tls_read(unsigned *size);
void tls_read_ack(unsigned size);
int tls_closed(void);
void tls_close(void);
#endif
