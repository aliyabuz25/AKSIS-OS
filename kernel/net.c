/* Bounded, polling IPv4 client for the bundled RTL8139/QEMU NAT adapter.
 * No IRQs, blocking socket calls, dynamic packet allocations or host proxy.
 * Protocol references and supported limits are documented in NETWORK.md. */
#include "net.h"
#include "tls.h"
#include "web.h"
#include "ssh.h"
#include "kernel_info.h"

net_info_t net_info;
static uint16_t io;
static uint8_t rx_ring[8192+16+1536] __attribute__((aligned(256)));
static uint8_t tx_ring[4][1536] __attribute__((aligned(256)));
static unsigned rx_pos, tx_slot;
static uint8_t packet[1536], outgoing[1536], router_mac[6];
static uint64_t last_poll, last_arp;
static uint16_t ip_id, source_port=49152, dns_id=0x5310;
static int state;
static const char *error_text="";
static char http_data[NET_HTTP_CAP+1], http_request[8192], hostname[254];
static unsigned data_len, request_len;
static uint32_t peer_ip, sequence, acknowledged, receive_next, initial_seq;
static uint16_t peer_port;
static uint64_t started, last_send;
static unsigned attempts;
static int request_sent;
static int secure;
static uint8_t tls_pending[1460];
static unsigned tls_pending_size,tls_request_offset;
static uint16_t peer_window=8192;

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0]<<8)|p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)be16(p)<<16)|be16(p+2); }
static void put16(uint8_t *p,unsigned n) { p[0]=n>>8; p[1]=n; }
static void put32(uint8_t *p,uint32_t n) { put16(p,n>>16); put16(p+2,n); }
static inline uint32_t in32(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void out32(uint16_t p,uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p):"memory"); }
static uint32_t pci_read(unsigned b,unsigned d,unsigned f,unsigned reg) {
    out32(0xcf8,0x80000000u|(b<<16)|(d<<11)|(f<<8)|(reg&0xfc)); return in32(0xcfc);
}
static uint32_t dma_address(const void *p) {
    uint64_t address=(uint64_t)p-g_sysinfo.kernel_virt_base+g_sysinfo.kernel_phys_base;
    return address<0x100000000ULL?(uint32_t)address:0;
}
static unsigned sum_words(const uint8_t *p,unsigned n,unsigned sum) {
    while(n>=2) { sum+=be16(p); p+=2; n-=2; }
    if(n) sum+=(unsigned)*p<<8;
    return sum;
}
static uint16_t checksum(const uint8_t *p,unsigned n,unsigned sum) {
    sum=sum_words(p,n,sum);
    while(sum>>16) sum=(sum&65535)+(sum>>16);
    return (uint16_t)~sum;
}
static int same_mac(const uint8_t *a,const uint8_t *b) { return !kmemcmp(a,b,6); }
static int transmit(const uint8_t *p,unsigned n) {
    if(!io||!net_info.enabled||!net_info.link||n>1514) return 0;
    unsigned slot=tx_slot;
    if(!(in32(io+0x10+slot*4)&0x2000)) return 0; /* Never overwrite DMA in flight. */
    unsigned size=n<60?60:n;
    kmemcpy(tx_ring[slot],p,n);
    if(size>n) kmemset(tx_ring[slot]+n,0,size-n);
    __asm__ volatile("":::"memory");
    out32(io+0x20+slot*4,dma_address(tx_ring[slot]));
    out32(io+0x10+slot*4,size);
    tx_slot=(slot+1)%4; net_info.tx_packets++; return 1;
}
static void ethernet(uint16_t type,const uint8_t *dest) {
    kmemcpy(outgoing,dest,6); kmemcpy(outgoing+6,net_info.mac,6); put16(outgoing+12,type);
}
static void arp_send(uint16_t op,uint32_t target,const uint8_t *mac) {
    static const uint8_t broadcast[6]={255,255,255,255,255,255};
    ethernet(0x806,op==1?broadcast:mac);
    uint8_t *a=outgoing+14; kmemset(a,0,28);
    put16(a,1); put16(a+2,0x800); a[4]=6; a[5]=4; put16(a+6,op);
    kmemcpy(a+8,net_info.mac,6); put32(a+14,net_info.ip);
    if(op==2) kmemcpy(a+18,mac,6);
    put32(a+24,target); transmit(outgoing,42);
}
static uint8_t *ipv4(unsigned proto,uint32_t dest,unsigned payload) {
    ethernet(0x800,router_mac);
    uint8_t *ip=outgoing+14; kmemset(ip,0,20);
    ip[0]=0x45; put16(ip+2,20+payload); put16(ip+4,++ip_id); put16(ip+6,0x4000);
    ip[8]=64; ip[9]=proto; put32(ip+12,net_info.ip); put32(ip+16,dest);
    put16(ip+10,checksum(ip,20,0)); return ip+20;
}
static unsigned pseudo(uint32_t src,uint32_t dst,unsigned proto,unsigned size) {
    return (src>>16)+(src&65535)+(dst>>16)+(dst&65535)+proto+size;
}
static int tcp_send(unsigned flags,uint32_t seq,const char *data,unsigned size) {
    if(!net_info.gateway) return 0;
    unsigned header=(flags&2)?24:20;
    uint8_t *tcp=ipv4(6,peer_ip,header+size); kmemset(tcp,0,header);
    put16(tcp,source_port); put16(tcp+2,peer_port); put32(tcp+4,seq);
    put32(tcp+8,(flags&16)?receive_next:0); tcp[12]=(header/4)<<4; tcp[13]=flags;
    put16(tcp+14,8192); /* A small window keeps the polling RX ring bounded. */
    if(flags&2) { tcp[20]=2; tcp[21]=4; put16(tcp+22,1460); }
    if(size) kmemcpy(tcp+header,data,size);
    put16(tcp+16,checksum(tcp,header+size,pseudo(net_info.ip,peer_ip,6,header+size)));
    return transmit(outgoing,14+20+header+size);
}
int net_send_tcp(uint32_t dest_ip, uint16_t src_port, uint16_t dst_port,
                 unsigned flags, uint32_t seq, uint32_t ack,
                 const void *data, unsigned size) {
    if (!net_info.gateway || !net_info.enabled || !net_info.link) return 0;
    unsigned header = (flags & 2) ? 24 : 20;
    uint8_t *tcp = ipv4(6, dest_ip, header + size);
    kmemset(tcp, 0, header);
    put16(tcp, src_port);
    put16(tcp + 2, dst_port);
    put32(tcp + 4, seq);
    put32(tcp + 8, (flags & 16) ? ack : 0);
    tcp[12] = (header / 4) << 4;
    tcp[13] = flags;
    put16(tcp + 14, 8192);
    if (flags & 2) { tcp[20] = 2; tcp[21] = 4; put16(tcp + 22, 1460); }
    if (size && data) kmemcpy(tcp + header, data, size);
    put16(tcp + 16, checksum(tcp, header + size, pseudo(net_info.ip, dest_ip, 6, header + size)));
    return transmit(outgoing, 14 + 20 + header + size);
}
static void fail(const char *message) {
    if(state==NET_RECEIVE) tcp_send(0x14,sequence,NULL,0);
    state=NET_ERROR; error_text=message;
}
void net_http_cancel(void) {
    if(state==NET_RECEIVE) tcp_send(0x14,sequence,NULL,0);
    state=NET_IDLE; data_len=0; http_data[0]=0; tls_pending_size=0;
}
static void tls_pump(void) {
    if(state!=NET_RECEIVE) return;
    const char *error=tls_error();
    if(error) { fail(error); return; }
    for(unsigned budget=0;budget<8;budget++) {
        unsigned n; const uint8_t *data=tls_read(&n);
        if(!data||!n) break;
        if(n>NET_HTTP_CAP-data_len) { fail("Sayfa 2 MB indirme sınırını aşıyor."); return; }
        kmemcpy(http_data+data_len,data,n); data_len+=n; http_data[data_len]=0;
        tls_read_ack(n);
    }
    if(tls_request_offset<request_len)
        tls_request_offset+=tls_write(http_request+tls_request_offset,request_len-tls_request_offset);
    if(tls_closed()) {
        if(tls_error()) fail(tls_error());
        else if(data_len) state=NET_DONE;
        else fail("TLS sunucusu yanıt göndermedi.");
    }
}
static void tls_transmit(uint64_t now) {
    tls_pump(); if(state!=NET_RECEIVE) return;
    if(tls_pending_size) {
        if(now-last_send>=1000) {
            if(attempts++>=5) { fail("TLS paketleri onaylanmadı."); return; }
            tcp_send(0x18,sequence-tls_pending_size,(const char *)tls_pending,tls_pending_size);
            last_send=now;
        }
        return;
    }
    unsigned n; const uint8_t *p=tls_send_record(&n);
    if(!p||!n||!peer_window) return;
    if(n>sizeof(tls_pending)) n=sizeof(tls_pending);
    if(n>peer_window) n=peer_window;
    kmemcpy(tls_pending,p,n);
    if(tcp_send(0x18,sequence,(const char *)tls_pending,n)) {
        tls_pending_size=n; sequence+=n; tls_sent_record(n);
        attempts=0; last_send=now;
    }
}
static void dns_send(void) {
    uint8_t query[300]; kmemset(query,0,sizeof(query));
    put16(query,dns_id); put16(query+2,0x100); put16(query+4,1);
    unsigned pos=12,at=0;
    while(hostname[at]) {
        unsigned begin=at; while(hostname[at]&&hostname[at]!='.') at++;
        unsigned len=at-begin; if(!len||len>63||pos+len+6>=sizeof(query)) { fail("Geçersiz alan adı."); return; }
        query[pos++]=len; kmemcpy(query+pos,hostname+begin,len); pos+=len;
        if(hostname[at]=='.') at++;
    }
    query[pos++]=0; put16(query+pos,1); put16(query+pos+2,1); pos+=4;
    uint8_t *udp=ipv4(17,net_info.dns,pos+8); kmemset(udp,0,8);
    put16(udp,source_port); put16(udp+2,53); put16(udp+4,pos+8); kmemcpy(udp+8,query,pos);
    uint16_t c=checksum(udp,pos+8,pseudo(net_info.ip,net_info.dns,17,pos+8));
    put16(udp+6,c?c:65535); transmit(outgoing,42+pos);
}
/* Skip DNS labels without following compression pointers (bounded parse). */
static unsigned dns_skip(const uint8_t *p,unsigned n,unsigned pos) {
    for(unsigned labels=0;labels<128&&pos<n;labels++) {
        unsigned len=p[pos++]; if(!len) return pos;
        if((len&0xc0)==0xc0) return pos<n?pos+1:0;
        if(len>63||pos+len>n) return 0;
        pos+=len;
    }
    return 0;
}
static void dns_receive(const uint8_t *p,unsigned n) {
    if(n<12||be16(p)!=dns_id||!(p[2]&0x80)) return;
    if(p[3]&15) { fail("Alan adı çözülemedi."); return; }
    if(p[2]&2) { fail("DNS yanıtı bu istemci için fazla büyük."); return; }
    unsigned pos=12,questions=be16(p+4),answers=be16(p+6);
    if(questions!=1||answers>128) return;
    pos=dns_skip(p,n,pos); if(!pos||pos+4>n) return; pos+=4;
    for(unsigned i=0;i<answers;i++) {
        pos=dns_skip(p,n,pos); if(!pos||pos+10>n) return;
        unsigned type=be16(p+pos),cls=be16(p+pos+2),len=be16(p+pos+8); pos+=10;
        if(pos+len>n) return;
        if(type==1&&cls==1&&len==4) {
            peer_ip=be32(p+pos); state=NET_CONNECT; attempts=0; last_send=0; return;
        }
        pos+=len;
    }
    fail("Bu alan adı için IPv4 adresi bulunamadı.");
}
static void tcp_receive(const uint8_t *p,unsigned n) {
    if(n<20||be16(p)!=peer_port||be16(p+2)!=source_port) return;
    unsigned header=(p[12]>>4)*4; if(header<20||header>n) return;
    if(checksum(p,n,pseudo(peer_ip,net_info.ip,6,n))) return;
    uint32_t seq=be32(p+4),ack=be32(p+8); unsigned flags=p[13],len=n-header;
    peer_window=be16(p+14);
    if(state==NET_CONNECT) {
        if((flags&0x14)==0x14&&ack==initial_seq+1) { fail("Sunucu bağlantıyı reddetti."); return; }
        if((flags&0x12)!=0x12||ack!=initial_seq+1) return;
        receive_next=seq+1; sequence=ack; acknowledged=ack;
        tcp_send(0x10,sequence,NULL,0); state=NET_RECEIVE; request_sent=0; attempts=0; last_send=0;
        return;
    }
    if(state!=NET_RECEIVE&&state!=NET_DONE) return;
    if(flags&4) { if(seq==receive_next&&state!=NET_DONE) fail("Sunucu bağlantıyı sıfırladı."); return; }
    if((flags&16)&&(int32_t)(ack-acknowledged)>0&&(int32_t)(sequence-ack)>=0) {
        acknowledged=ack;
        if(secure&&ack==sequence) tls_pending_size=0;
    }
    if(state==NET_DONE) {
        if((flags&1)&&seq+len==receive_next) { receive_next++; tcp_send(0x11,sequence++,NULL,0); }
        else if(flags&1) tcp_send(0x10,sequence,NULL,0);
        return;
    }
    if(flags&2) { tcp_send(0x10,sequence,NULL,0); return; }
    /* Re-ACK missing data. Accept overlap, never duplicate received bytes. */
    if((int32_t)(seq-receive_next)>0) { tcp_send(0x10,sequence,NULL,0); return; }
    unsigned skip=receive_next-seq;
    if(skip<len) {
        unsigned copy=len-skip;
        if(secure) {
            unsigned used=0;
            while(used<copy&&state==NET_RECEIVE) {
                tls_pump();
                unsigned part=tls_feed(p+header+skip+used,copy-used);
                if(!part) break;
                used+=part;
            }
            receive_next+=used; tls_pump();
        } else {
            if(copy>NET_HTTP_CAP-data_len) { fail("Sayfa 2 MB indirme sınırını aşıyor."); return; }
            kmemcpy(http_data+data_len,p+header+skip,copy); data_len+=copy; http_data[data_len]=0;
            receive_next+=copy;
        }
    }
    if((flags&1)&&seq+len==receive_next) {
        receive_next++;
        if(secure&&state!=NET_DONE) {
            tls_pump();
            if(state==NET_RECEIVE&&(web_http_complete(http_data,data_len)||data_len>12)) state=NET_DONE;
            if(state!=NET_DONE) {
                if(data_len>0) state=NET_DONE;
                else if(state!=NET_ERROR) { fail("TLS bağlantısı yanıt tamamlanmadan kesildi."); return; }
            }
        } else state=NET_DONE;
        if(secure) {
            tls_close(); unsigned size; const uint8_t *record=tls_send_record(&size);
            if(record&&size&&size<=1460&&tcp_send(0x18,sequence,(const char *)record,size)) {
                sequence+=size; tls_sent_record(size);
            }
        }
        tcp_send(0x11,sequence,NULL,0); sequence++;
    } else if(len) tcp_send(0x10,sequence,NULL,0);
}
static void receive_packet(const uint8_t *frame,unsigned size) {
    if(size<14) return;
    const uint8_t *p=frame+14; unsigned n=size-14;
    if(be16(frame+12)==0x806) {
        if(n<28||be16(p)!=1||be16(p+2)!=0x800||p[4]!=6||p[5]!=4) return;
        uint32_t sender=be32(p+14),target=be32(p+24); unsigned op=be16(p+6);
        if(target!=net_info.ip||!same_mac(p+8,frame+6)) return;
        if(sender==net_info.router&&(op==1||op==2)) {
            kmemcpy(router_mac,p+8,6); net_info.gateway=1;
        }
        if(op==1) arp_send(2,sender,p+8);
        return;
    }
    if(be16(frame+12)!=0x800||n<20||(p[0]>>4)!=4) return;
    unsigned ihl=(p[0]&15)*4,total=be16(p+2);
    if(ihl<20||ihl>n||total<ihl||total>n||checksum(p,ihl,0)||(be16(p+6)&0x3fff)) return;
    if(be32(p+16)!=net_info.ip) return;
    uint32_t src=be32(p+12); unsigned proto=p[9]; n=total-ihl; p+=ihl;
    if(proto==17&&state==NET_DNS&&src==net_info.dns&&n>=8&&be16(p)==53&&be16(p+2)==source_port) {
        unsigned len=be16(p+4); if(len<8||len>n) return;
        if(be16(p+6)&&checksum(p,len,pseudo(src,net_info.ip,17,len))) return;
        dns_receive(p+8,len-8);
    } else if(proto==6&&src==peer_ip) tcp_receive(p,n);
    else if(proto==6) {
        if(n>=4) {
            uint16_t dst_port = be16(p+2);
            if(dst_port==22) ssh_server_receive(src,p,n);
            else ssh_client_receive(p,n);
        }
    }
}
void net_init(void) {
    net_info.ip=0x0a00020f; net_info.router=0x0a000202; net_info.dns=0x0a000203;
    for(unsigned b=0;b<256&&!io;b++) for(unsigned d=0;d<32&&!io;d++) {
        if((pci_read(b,d,0,0)&65535)==65535) continue;
        unsigned functions=(pci_read(b,d,0,12)&0x800000)?8:1;
        for(unsigned f=0;f<functions;f++) if(pci_read(b,d,f,0)==0x813910ec) {
            uint32_t bar=pci_read(b,d,f,16); if(!(bar&1)||!(bar&0xfffc)) continue;
            io=bar&0xfffc;
            uint32_t command=pci_read(b,d,f,4)&65535;
            out32(0xcf8,0x80000000u|(b<<16)|(d<<11)|(f<<8)|4); out32(0xcfc,command|5);
            break;
        }
    }
    if(!io||!dma_address(rx_ring)||!dma_address(tx_ring)||!g_sysinfo.tsc_freq_hz) { io=0; return; }
    outb(io+0x52,0); outb(io+0x37,0x10);
    unsigned retry=100000; while((inb(io+0x37)&0x10)&&retry) retry--;
    if(!retry) { io=0; return; }
    for(int i=0;i<6;i++) net_info.mac[i]=inb(io+i);
    outw(io+0x3c,0); outw(io+0x3e,0xffff);
    out32(io+0x30,dma_address(rx_ring));
    out32(io+0x44,0xe700|0x80|0x0a); /* 8K ring, no-wrap, own MAC + broadcast. */
    out32(io+0x40,0x03000700); outb(io+0x37,0x0c);
    net_info.present=net_info.enabled=1;
    net_info.link=(inw(io+0x64)&4)!=0;
}
void net_enable(int enabled) {
    if(!net_info.present) return;
    if(!enabled) net_http_cancel();
    net_info.enabled=!!enabled; net_info.gateway=0; last_arp=0;
}
void net_poll(void) {
    if(!io) return;
    uint64_t now=get_uptime_ms();
    if(now==last_poll) return; /* At most 1,000 polls/s; bounded work per poll. */
    last_poll=now; net_info.link=(inw(io+0x64)&4)!=0;
    if(!net_info.link) { net_info.gateway=0; if(state>NET_IDLE&&state<NET_DONE) fail("Host ağ bağlantısı kesildi."); }
    for(unsigned budget=0;budget<24&&!(inb(io+0x37)&1);budget++) {
        __asm__ volatile("":::"memory");
        uint8_t *head=rx_ring+rx_pos;
        unsigned status=head[0]|head[1]<<8, size=head[2]|head[3]<<8;
        if(size==0xfff0) break; /* DMA has not finished the current packet. */
        if(size<4||size>1522) { net_info.dropped++; outb(io+0x37,4); rx_pos=0; out32(io+0x30,dma_address(rx_ring)); outb(io+0x37,12); break; }
        if((status&1)&&size-4<=sizeof(packet)) {
            kmemcpy(packet,head+4,size-4); net_info.rx_packets++;
            /* Release ring space before transmitting an ACK. */
            rx_pos=ALIGN(rx_pos+size+4,4)%8192; outw(io+0x38,(uint16_t)(rx_pos-16));
            if(net_info.enabled) receive_packet(packet,size-4);
        } else { rx_pos=ALIGN(rx_pos+size+4,4)%8192; outw(io+0x38,(uint16_t)(rx_pos-16)); net_info.dropped++; }
    }
    outw(io+0x3e,0xffff);
    ssh_poll();
    if(!net_info.enabled||!net_info.link) return;
    if(!net_info.gateway&&(!last_arp||now-last_arp>=1000)) { arp_send(1,net_info.router,NULL); last_arp=now; }
    if(state<=NET_IDLE||state>=NET_DONE) return;
    if(now-started>30000) { fail("Bağlantı zaman aşımına uğradı."); return; }
    if(!net_info.gateway) return;
    if(state==NET_DNS&&(!last_send||now-last_send>=1200)) {
        if(attempts++>=4) { fail("DNS yanıt vermedi. Host internetini kontrol et."); return; }
        dns_send(); last_send=now;
    } else if(state==NET_CONNECT&&(!last_send||now-last_send>=1000)) {
        if(attempts++>=5) { fail("HTTP sunucusuna ulaşılamadı."); return; }
        tcp_send(2,initial_seq,NULL,0); last_send=now;
    } else if(state==NET_RECEIVE&&secure) {
        tls_transmit(now);
    } else if(state==NET_RECEIVE&&(!request_sent||(acknowledged!=sequence&&now-last_send>=1000))) {
        if(attempts++>=5) { fail("HTTP isteği onaylanmadı."); return; }
        if(tcp_send(0x18,initial_seq+1,http_request,request_len)) {
            sequence=initial_seq+1+request_len; request_sent=1; last_send=now;
        }
    }
}
int net_web_start(const char *host,uint16_t port,const char *request,int use_tls) {
    net_http_cancel(); error_text="";
    if(!net_info.present) { fail("Sanal ağ kartı yok. Yeni UTM paketini kullan."); return 0; }
    if(!net_info.enabled||!net_info.link) { fail("Host ağı kapalı. Ağ penceresinden bağlan."); return 0; }
    if(kstrlen(host)>253||!host[0]||kstrlen(request)>8191||!port) { fail("Geçersiz adres."); return 0; }
    kstrcpy(hostname,host); kstrcpy(http_request,request); request_len=kstrlen(request);
    secure=!!use_tls; tls_pending_size=tls_request_offset=0; peer_window=8192;
    if(secure&&!tls_start(host)) { fail(tls_error()); return 0; }
    peer_port=port; peer_ip=0; receive_next=0; sequence=acknowledged=0;
    initial_seq=(uint32_t)get_uptime_ms()*65537u+0x53480000u;
    if(++source_port<49152) source_port=49152;
    dns_id++; started=get_uptime_ms(); last_send=0; attempts=0; request_sent=0;
    /* Numeric IPv4 bypasses DNS; names use the NAT resolver. */
    uint32_t ip=0; unsigned count=0,value=0; int numeric=1,digits=0;
    for(unsigned i=0;;i++) {
        char c=host[i];
        if(c>='0'&&c<='9') { value=value*10+c-'0'; digits++; if(value>255) { numeric=0; break; } }
        else if(c=='.'||!c) {
            if(!digits||count==4) { numeric=0; break; }
            ip=(ip<<8)|value; count++; value=0; digits=0; if(!c) break;
        } else { numeric=0; break; }
    }
    if(numeric&&count==4) { peer_ip=ip; state=NET_CONNECT; } else state=NET_DNS;
    return 1;
}
int net_http_start(const char *host,uint16_t port,const char *request) { return net_web_start(host,port,request,0); }
int net_http_secure(void) { return secure; }
int net_http_state(void) { return state; }
const char *net_http_error(void) { return error_text; }
const char *net_http_data(void) { return http_data; }
unsigned net_http_size(void) { return data_len; }
void net_ip_text(uint32_t ip,char *out) {
    char part[16]; out[0]=0;
    for(int i=3;i>=0;i--) { kitoa((ip>>(i*8))&255,part); kstrcat(out,part); if(i) kstrcat(out,"."); }
}
