#ifndef SHENLONG_WEB_H
#define SHENLONG_WEB_H
#include "kernel.h"
#define WEB_URL_MAX 512
#define WEB_LINES 384
#define WEB_LINKS 48
typedef struct { char host[254],path[WEB_URL_MAX]; uint16_t port; int secure; } web_url_t;
typedef struct { char text[100]; int link, heading; } web_line_t;
typedef struct { char url[WEB_URL_MAX]; } web_link_t;
typedef struct {
    char title[128];
    web_line_t lines[WEB_LINES]; web_link_t links[WEB_LINKS];
    unsigned count,link_count; int truncated;
} web_page_t;
int web_url_parse(const char *url,web_url_t *out,const char **error);
int web_url_resolve(const char *base,const char *href,char *out);
int web_http_decode(const char *raw,unsigned size,char *body,unsigned cap,
                    unsigned *body_size,int *status,char *location,const char **error);
int web_response_decode(const char *raw,unsigned size,char *body,unsigned cap,
                    unsigned *body_size,int *status,char *location,const char **error,int *media);
void web_page_parse(const char *body,unsigned size,web_page_t *page,unsigned columns);
int web_http_complete(const char *raw,unsigned size);
int web_text_utf8(const char *raw,unsigned raw_size,char *body,unsigned *size,unsigned cap);
#endif
