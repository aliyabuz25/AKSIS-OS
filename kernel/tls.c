/* Native TLS 1.2. No proxy, hostname bypass or certificate-error override. */
#include "tls.h"
#include "kernel_info.h"
#include "bearssl.h"
#include "tls_roots.h"
static br_ssl_client_context client;
static br_x509_minimal_context validator;
static uint8_t buffers[BR_SSL_BUFSIZE_BIDI];
static const char *start_error;
static int entropy(uint8_t *out,unsigned size) {
    uint32_t a,b,c,d;
    __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(1),"c"(0));
    if(!(c&(1u<<30))) return 0;
    for(unsigned i=0;i<size;i+=8) {
        uint64_t value=0; unsigned char ok=0;
        for(int tries=0;tries<32&&!ok;tries++)
            __asm__ volatile("rdrand %0; setc %1":"=r"(value),"=qm"(ok));
        if(!ok) return 0;
        unsigned n=size-i<8?size-i:8; kmemcpy(out+i,&value,n);
    }
    return 1;
}
int tls_start(const char *host) {
    uint8_t seed[48]; start_error=NULL;
    if(!entropy(seed,sizeof(seed))) { start_error="TLS: güvenli rastgelelik yok; yeni UTM paketini kullan."; return 0; }
    int64_t epoch=g_sysinfo.boot_time+(int64_t)(get_uptime_ms()/1000);
    if(epoch<1704067200) { start_error="TLS: sistem tarihini kontrol et."; return 0; }
    br_ssl_client_init_full(&client,&validator,TAs,TAs_NUM);
    static const uint16_t suites[]={
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
        BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
        BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
        BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
        BR_TLS_RSA_WITH_AES_256_CBC_SHA256,
        BR_TLS_RSA_WITH_AES_128_CBC_SHA,
        BR_TLS_RSA_WITH_AES_256_CBC_SHA
    };
    br_ssl_engine_set_suites(&client.eng,suites,sizeof(suites)/sizeof(suites[0]));
    br_ssl_engine_set_versions(&client.eng,BR_TLS12,BR_TLS12);
    br_x509_minimal_set_time(&validator,(uint32_t)(epoch/86400+719528),(uint32_t)(epoch%86400));
    br_ssl_engine_set_buffer(&client.eng,buffers,sizeof(buffers),1);
    br_ssl_engine_inject_entropy(&client.eng,seed,sizeof(seed)); kmemset(seed,0,sizeof(seed));
    if(!br_ssl_client_reset(&client,host,0)) { start_error="TLS başlatılamadı."; return 0; }
    return 1;
}
const char *tls_error(void) {
    if(start_error) return start_error;
    int e=br_ssl_engine_last_error(&client.eng);
    if(!e) return NULL;
    if(e==BR_ERR_X509_BAD_SERVER_NAME) return "TLS: sertifika alan adıyla eşleşmiyor.";
    if(e==BR_ERR_X509_EXPIRED) return "TLS: sertifikanın tarihi geçersiz.";
    if(e==BR_ERR_X509_NOT_TRUSTED) return "TLS: sertifika güvenilir bir köke bağlı değil.";
    if(e>=32&&e<64) return "TLS: sertifika doğrulanamadı.";
    return "TLS: güvenli bağlantı kurulamadı.";
}
unsigned tls_feed(const void *data,unsigned size) {
    size_t n; uint8_t *p=br_ssl_engine_recvrec_buf(&client.eng,&n);
    if(!p) return 0; if(n>size) n=size;
    kmemcpy(p,data,n); br_ssl_engine_recvrec_ack(&client.eng,n); return n;
}
const uint8_t *tls_send_record(unsigned *size) {
    size_t n; const uint8_t *p=br_ssl_engine_sendrec_buf(&client.eng,&n); *size=n; return p;
}
void tls_sent_record(unsigned size) { br_ssl_engine_sendrec_ack(&client.eng,size); }
unsigned tls_write(const void *data,unsigned size) {
    size_t n; uint8_t *p=br_ssl_engine_sendapp_buf(&client.eng,&n);
    if(!p) return 0; if(n>size) n=size;
    kmemcpy(p,data,n); br_ssl_engine_sendapp_ack(&client.eng,n);
    br_ssl_engine_flush(&client.eng,0); return n;
}
const uint8_t *tls_read(unsigned *size) {
    size_t n; const uint8_t *p=br_ssl_engine_recvapp_buf(&client.eng,&n); *size=n; return p;
}
void tls_read_ack(unsigned size) { br_ssl_engine_recvapp_ack(&client.eng,size); }
void tls_close(void) { br_ssl_engine_close(&client.eng); }
int tls_closed(void) {
    /* BearSSL may still have our reply alert queued after authenticating the
       peer's close_notify. A simultaneous TCP FIN does not invalidate it. */
    return client.eng.close_received||br_ssl_engine_current_state(&client.eng)==BR_SSL_CLOSED;
}
