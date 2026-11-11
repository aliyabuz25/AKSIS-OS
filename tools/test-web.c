#include "../kernel/web.h"
extern int printf(const char *,...);
void *kmemset(void *p,int c,size_t n) { uint8_t *s=p; while(n--) *s++=c; return p; }
void *kmemcpy(void *p,const void *q,size_t n) { uint8_t *d=p; const uint8_t *s=q; while(n--) *d++=*s++; return p; }
int kmemcmp(const void *a,const void *b,size_t n) { const uint8_t *x=a,*y=b; while(n--) { if(*x!=*y) return *x-*y; x++; y++; } return 0; }
int kstrlen(const char *s) { int n=0; while(s[n]) n++; return n; }
int kstrcmp(const char *a,const char *b) { while(*a&&*a==*b) { a++; b++; } return (uint8_t)*a-(uint8_t)*b; }
char *kstrcpy(char *a,const char *b) { char *p=a; while((*p++=*b++)) {} return a; }
char *kstrcat(char *a,const char *b) { kstrcpy(a+kstrlen(a),b); return a; }
char *kstrchr(const char *a,char c) { while(*a) { if(*a==c) return (char *)a; a++; } return NULL; }
void kitoa(int64_t n,char *out) { char b[32]; int at=0; do { b[at++]=n%10+'0'; n/=10; } while(n); int i=0; while(at) out[i++]=b[--at]; out[i]=0; }
#define CHECK(expr) do { if(!(expr)) { printf("FAIL web line %d: %s\n",__LINE__,#expr); return 1; } } while(0)
static web_page_t page;
static char big[50000];
int main(void) {
    web_url_t u; const char *error; char resolved[WEB_URL_MAX],body[4096],location[WEB_URL_MAX]; unsigned size; int status;
    CHECK(web_url_parse("http://example.com:8080/a?q=1#anchor",&u,&error));
    CHECK(!kstrcmp(u.host,"example.com")&&u.port==8080&&!kstrcmp(u.path,"/a?q=1"));
    CHECK(web_url_parse("https://example.com/",&u,&error)&&u.secure&&u.port==443);
    CHECK(web_url_resolve("https://host:8443/a/b","../next",resolved)&&!kstrcmp(resolved,"https://host:8443/next"));
    CHECK(web_url_resolve("https://host/a","//cdn.example/file",resolved)&&!kstrcmp(resolved,"https://cdn.example/file"));
    CHECK(!web_url_parse("http://example.com/\r\nX: inject",&u,&error));
    CHECK(!web_url_parse("http://user@example.com/",&u,&error));
    CHECK(!web_url_parse("http://host:65536/",&u,&error));
    CHECK(web_url_resolve("http://host:81/a/b","../next?q=1",resolved)&&!kstrcmp(resolved,"http://host:81/next?q=1"));
    CHECK(web_url_resolve("http://host/a","https://secure/",resolved)&&!kstrcmp(resolved,"https://secure/"));
    CHECK(!web_url_resolve("http://host/","javascript:alert(1)",resolved));
    const char *valid="HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello";
    CHECK(web_http_decode(valid,kstrlen(valid),body,sizeof(body),&size,&status,location,&error)&&size==5&&status==200&&!kstrcmp(body,"hello"));
    int media;
    const char binary[]="HTTP/1.1 200 OK\r\nContent-Type: video/mpeg\r\nContent-Length: 4\r\n\r\n\0\0\1\272";
    CHECK(web_response_decode(binary,sizeof(binary)-1,body,sizeof(body),&size,&status,location,&error,&media)&&media&&size==4);
    CHECK(!web_http_decode(binary,sizeof(binary)-1,body,sizeof(body),&size,&status,location,&error));
    const char *chunked="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3;x=y\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
    CHECK(web_http_decode(chunked,kstrlen(chunked),body,sizeof(body),&size,&status,location,&error)&&!kstrcmp(body,"abcde"));
    CHECK(web_http_complete(valid,kstrlen(valid))&&web_http_complete(chunked,kstrlen(chunked)));
    for(unsigned n=0;n<(unsigned)kstrlen(valid);n++) CHECK(!web_http_complete(valid,n));
    for(unsigned n=0;n<(unsigned)kstrlen(chunked);n++) CHECK(!web_http_complete(chunked,n));
    const char *unframed="HTTP/1.1 200 OK\r\n\r\nhello";
    CHECK(!web_http_complete(unframed,kstrlen(unframed)));
    const char *latin="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=ISO-8859-9\r\n\r\n";
    body[0]=(char)0xf6; body[1]=(char)0xfd; body[2]=(char)0xfe; size=3;
    CHECK(web_text_utf8(latin,kstrlen(latin),body,&size,sizeof(body))&&size==6&&!kstrcmp(body,"öış"));
    const char *bad[]={"HTTP/1.1 200 OK\r\nContent-Length: 1\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nshort",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nfffffffe\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nx"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) CHECK(!web_http_decode(bad[i],kstrlen(bad[i]),body,sizeof(body),&size,&status,location,&error));
    const char *html="<html><head><title>Native test</title></head><body><h1>Hello &amp; web</h1><script>SECRET</script><p>Türkçe &#351;</p><a href='/next?q=1&amp;x=2'>next</a></body></html>";
    web_page_parse(html,kstrlen(html),&page,80);
    CHECK(!kstrcmp(page.title,"Native test")&&page.link_count==1&&!kstrcmp(page.links[0].url,"/next?q=1&x=2"));
    CHECK(!kstrcmp(page.lines[0].text,"Hello & web")&&!kstrcmp(page.lines[1].text,"Türkçe ş"));
    for(unsigned i=0;i<sizeof(big);i++) big[i]=(i%20==19)?' ':'x';
    web_page_parse(big,sizeof(big),&page,60); CHECK(page.truncated&&page.count<=WEB_LINES);
    printf("PASS web: URL validation, relative links, HTTPS preservation, HTTP framing/chunks, malformed input, HTML/entities, bounded output.\n");
    return 0;
}
