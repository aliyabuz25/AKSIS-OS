/* Small bounded HTTP/HTML reader. Deliberately no scripting or TLS downgrade. */
#include "web.h"
static int lower(int c) { return c>='A'&&c<='Z'?c+32:c; }
static int eq(const char *a,const char *b,unsigned n) {
    for(unsigned i=0;i<n;i++) if(lower((uint8_t)a[i])!=lower((uint8_t)b[i])) return 0;
    return 1;
}
static int starts(const char *s,const char *prefix) {
    unsigned n=kstrlen(prefix); return (unsigned)kstrlen(s)>=n&&eq(s,prefix,n);
}
static int copy(char *dst,unsigned cap,const char *src,unsigned n) {
    if(n>=cap) return 0;
    kmemcpy(dst,src,n); dst[n]=0; return 1;
}
int web_url_parse(const char *url,web_url_t *u,const char **error) {
    *error="Geçersiz web adresi."; kmemset(u,0,sizeof(*u));
    char fixed_url[WEB_URL_MAX];
    if(!starts(url,"https://") && !starts(url,"http://")) {
        int has_dot = 0, has_space = 0;
        for(int i=0; url[i]; i++) {
            if(url[i] == '.') has_dot = 1;
            if(url[i] == ' ') has_space = 1;
        }
        if(!has_dot || has_space) {
            /* Akıllı Google Araması */
            kstrcpy(fixed_url, "https://www.google.com/search?q=");
            int fi = kstrlen(fixed_url);
            for(int i=0; url[i] && fi < WEB_URL_MAX - 2; i++) {
                fixed_url[fi++] = (url[i] == ' ') ? '+' : url[i];
            }
            fixed_url[fi] = 0;
            url = fixed_url;
        } else {
            /* Otomatik https:// ekle */
            kstrcpy(fixed_url, "https://");
            kstrcat(fixed_url, url);
            url = fixed_url;
        }
    }
    u->secure=starts(url,"https://");
    unsigned len=kstrlen(url); if(len>=WEB_URL_MAX) { *error="Adres çok uzun."; return 0; }
    for(unsigned i=0;i<len;i++) if((uint8_t)url[i]<=32||(uint8_t)url[i]>=127||url[i]=='\\') return 0;
    const char *p=url+(u->secure?8:7),*begin=p;
    while(*p&&*p!='/'&&*p!=':'&&*p!='?'&&*p!='#') {
        char c=*p;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return 0;
        p++;
    }
    if(p==begin||!copy(u->host,sizeof(u->host),begin,p-begin)) return 0;
    unsigned label=0;
    for(unsigned i=0;u->host[i];i++) {
        if(u->host[i]=='.') { if(!label||label>63) return 0; label=0; } else label++;
    }
    if(!label||label>63) return 0;
    u->port=u->secure?443:80;
    if(*p==':') {
        unsigned port=0,digits=0; p++;
        while(*p>='0'&&*p<='9') { port=port*10+*p++-'0'; if(port>65535) return 0; digits++; }
        if(!digits||!port) return 0;
        u->port=port;
    }
    if(*p&&*p!='/'&&*p!='?'&&*p!='#') return 0;
    unsigned n=0;
    if(*p!='/') u->path[n++]='/';
    while(*p&&*p!='#') { if(n+1>=sizeof(u->path)) return 0; u->path[n++]=*p++; }
    u->path[n]=0; *error=""; return 1;
}
int web_url_resolve(const char *base,const char *href,char *out) {
    if(!href[0]) return 0;
    if(starts(href,"http://")||starts(href,"https://")) return copy(out,WEB_URL_MAX,href,kstrlen(href));
    if(href[0]=='/'&&href[1]=='/') {
        int secure=starts(base,"https://");
        if(kstrlen(href)+(secure?6:5)>=WEB_URL_MAX) return 0;
        kstrcpy(out,secure?"https:":"http:"); kstrcat(out,href); return 1;
    }
    for(unsigned i=0;href[i]&&href[i]!='/';i++) if(href[i]==':') return 0;
    if(href[0]=='#') return copy(out,WEB_URL_MAX,base,kstrlen(base));
    web_url_t url; const char *error;
    if(!web_url_parse(base,&url,&error)) return 0;
    char origin[280],port[16]; kstrcpy(origin,url.secure?"https://":"http://"); kstrcat(origin,url.host);
    if(url.port!=(url.secure?443:80)) { kstrcat(origin,":"); kitoa(url.port,port); kstrcat(origin,port); }
    char path[WEB_URL_MAX]; path[0]=0;
    if(href[0]!='/') {
        kstrcpy(path,url.path);
        char *query=kstrchr(path,'?'); if(query) *query=0;
        if(href[0]!='?') { int end=kstrlen(path); while(end&&path[end-1]!='/') end--; path[end]=0; }
    }
    if(kstrlen(path)+kstrlen(href)>=WEB_URL_MAX) return 0;
    kstrcat(path,href);
    /* Resolve dot segments before query/fragment, preserving trailing slash. */
    char normalized[WEB_URL_MAX]; unsigned at=0,n=0;
    while(path[at]&&path[at]!='?'&&path[at]!='#') {
        if(path[at]=='/') { if(!n||normalized[n-1]!='/') normalized[n++]='/'; at++; continue; }
        unsigned begin=at; while(path[at]&&path[at]!='/'&&path[at]!='?'&&path[at]!='#') at++;
        unsigned len=at-begin;
        if(len==1&&path[begin]=='.') continue;
        if(len==2&&path[begin]=='.'&&path[begin+1]=='.') {
            if(n>1) n--; while(n>1&&normalized[n-1]!='/') n--; continue;
        }
        kmemcpy(normalized+n,path+begin,len); n+=len;
    }
    while(path[at]) normalized[n++]=path[at++]; normalized[n]=0;
    if(kstrlen(origin)+n>=WEB_URL_MAX) return 0;
    kstrcpy(out,origin); kstrcat(out,normalized); return 1;
}
static int decimal(const char *p,unsigned n,unsigned *out) {
    if(!n) return 0; unsigned value=0;
    for(unsigned i=0;i<n;i++) { if(p[i]<'0'||p[i]>'9'||value>10000000) return 0; value=value*10+p[i]-'0'; }
    *out=value; return 1;
}
/* RFC 9112 9.8: authenticated HTTP framing can prove completeness even when
   a TLS peer omits close_notify. Close-delimited responses cannot. */
int web_http_complete(const char *raw,unsigned size) {
    if(size<12) return 0;
    while(size > 4 && (*raw == '\r' || *raw == '\n')) { raw++; size--; }
    if(!eq(raw,"HTTP/",5)) return 0;
    const char *sp = raw;
    while(sp < raw + size && *sp != ' ') sp++;
    if(sp >= raw + size) return 0;
    while(sp < raw + size && *sp == ' ') sp++;
    unsigned code; if(!decimal(sp,3,&code)) return 0;
    unsigned pos=0,length=0; int has_length=0,chunked=0,headers=0;
    while(pos+1<size&&!(raw[pos]=='\r'&&raw[pos+1]=='\n')) pos++;
    if(pos+1>=size) {
        /* Tek \n satır sonunu da tolere et */
        pos=0; while(pos<size&&raw[pos]!='\n') pos++;
        if(pos>=size) return 0; pos++;
    } else pos+=2;
    while(pos+1<size&&pos<8192) {
        if(raw[pos]=='\r'&&raw[pos+1]=='\n') { pos+=2; headers=1; break; }
        if(raw[pos]=='\n') { pos+=1; headers=1; break; }
        unsigned begin=pos; while(pos+1<size&&!(raw[pos]=='\r'&&raw[pos+1]=='\n')&&raw[pos]!='\n') pos++;
        if(pos>=size||pos>8192) return 0;
        unsigned end=pos,colon=begin; while(colon<end&&raw[colon]!=':') colon++;
        if(colon==end) return 0;
        unsigned val=colon+1; while(val<end&&(raw[val]==' '||raw[val]=='\t')) val++;
        while(end>val&&(raw[end-1]==' '||raw[end-1]=='\t')) end--;
        if(colon-begin==14&&eq(raw+begin,"Content-Length",14)) {
            unsigned n; if(!decimal(raw+val,end-val,&n)||(has_length&&length!=n)) return 0;
            has_length=1; length=n;
        } else if(colon-begin==17&&eq(raw+begin,"Transfer-Encoding",17)) {
            if(chunked||end-val!=7||!eq(raw+val,"chunked",7)) return 0;
            chunked=1;
        }
        if(raw[pos]=='\r'&&pos+1<size&&raw[pos+1]=='\n') pos+=2;
        else if(raw[pos]=='\n') pos+=1;
    }
    if(!headers||(chunked&&has_length)) return 0;
    if(code==204||code==304) return 1;
    if(code>=300&&code<400) return 1; /* Yönlendirmelerde başlıklar geldiyse tamamlanmıştır */
    if(has_length) return (size-pos) >= length;
    if(!chunked) return 0;
    for(unsigned chunks=0;chunks<65536;chunks++) {
        unsigned n=0,digits=0;
        while(pos<size) {
            int c=lower(raw[pos]),v=c>='0'&&c<='9'?c-'0':(c>='a'&&c<='f'?c-'a'+10:-1);
            if(v<0) break;
            if(++digits>8||n>size/16) return 0; n=n*16+v; pos++;
        }
        if(!digits) return 0;
        if(pos<size&&raw[pos]==';') while(pos<size&&raw[pos]!='\r'&&raw[pos]!='\n') pos++;
        if(pos+1<size&&raw[pos]=='\r'&&raw[pos+1]=='\n') pos+=2;
        else if(pos<size&&raw[pos]=='\n') pos+=1;
        else return 0;
        if(!n) {
            if(pos+1<size&&raw[pos]=='\r'&&raw[pos+1]=='\n') return 1;
            if(pos<size&&raw[pos]=='\n') return 1;
            return 0;
        }
        if(n>size-pos) return 0; pos+=n;
        if(pos+1<size&&raw[pos]=='\r'&&raw[pos+1]=='\n') pos+=2;
        else if(pos<size&&raw[pos]=='\n') pos+=1;
        else return 0;
    }
    return 0;
}
int web_response_decode(const char *raw,unsigned size,char *body,unsigned cap,
                    unsigned *body_size,int *status,char *location,const char **error,int *media) {
    *media=0;
    *body_size=0; *status=0; location[0]=0; *error="Geçersiz HTTP yanıtı.";
    if(size<12) return 0;
    /* HTTP/1.0, HTTP/1.1, HTTP/2 veya tek boşluklu/boşluksuz yanıtları tolere et */
    const char *status_line = raw;
    while(size>4 && (*status_line=='\r' || *status_line=='\n')) { status_line++; size--; }
    if(!eq(status_line,"HTTP/",5)) return 0;
    const char *sp = status_line;
    while(sp<status_line+size && *sp!=' ') sp++;
    if(sp>=status_line+size) return 0;
    sp++; /* Boşluğu geç */
    while(sp<status_line+size && *sp==' ') sp++;
    unsigned code; if(!decimal(sp,3,&code)) return 0; *status=code;
    unsigned pos=sp-raw; while(pos+1<size&&!(raw[pos]=='\r'&&raw[pos+1]=='\n')) pos++;
    if(pos+1>=size) {
        /* Bazı sunucular tek \n kullanabilir */
        pos=sp-raw;
        while(pos<size&&raw[pos]!='\n') pos++;
        if(pos>=size) return 0;
        pos++;
    } else pos+=2;
    unsigned length=0; int has_length=0,chunked=0,headers_done=0;
    while(pos+1<size) {
        if(raw[pos]=='\r'&&raw[pos+1]=='\n') { pos+=2; headers_done=1; break; }
        if(raw[pos]=='\n') { pos+=1; headers_done=1; break; }
        unsigned begin=pos; while(pos+1<size&&!(raw[pos]=='\r'&&raw[pos+1]=='\n')) pos++;
        if(pos+1>=size||pos>8192) {
            /* \r\n yoksa tek \n ara */
            pos=begin;
            while(pos<size&&raw[pos]!='\n') pos++;
            if(pos>=size||pos>8192) break;
        }
        unsigned end=pos,colon=begin; while(colon<end&&raw[colon]!=':') colon++;
        if(colon==end) return 0;
        unsigned val=colon+1; while(val<end&&(raw[val]==' '||raw[val]=='\t')) val++;
        while(end>val&&(raw[end-1]==' '||raw[end-1]=='\t')) end--;
        unsigned key=colon-begin,n=end-val;
        if(key==14&&eq(raw+begin,"Content-Length",14)) {
            unsigned parsed; if(!decimal(raw+val,n,&parsed)||(has_length&&length!=parsed)) return 0;
            length=parsed; has_length=1;
        } else if(key==17&&eq(raw+begin,"Transfer-Encoding",17)) {
            if(n!=7||!eq(raw+val,"chunked",7)) { *error="Bu aktarım kodlaması desteklenmiyor."; return 0; }
            chunked=1;
        } else if(key==16&&eq(raw+begin,"Content-Encoding",16)) {
            if(n!=8||!eq(raw+val,"identity",8)) { *error="Sıkıştırılmış sayfalar desteklenmiyor."; return 0; }
        } else if(key==8&&eq(raw+begin,"Location",8)) {
            if(!copy(location,WEB_URL_MAX,raw+val,n)) return 0;
        } else if(key==12&&eq(raw+begin,"Content-Type",12)) {
            if(n>=10&&eq(raw+val,"video/mpeg",10)&&(n==10||raw[val+10]==';'||raw[val+10]==' ')) *media=1;
            /* text types, application/xhtml+xml, application/json vb. metin içeriklerini kabul et */
            else if(n>=5&&eq(raw+val,"text/",5)) { /* text content */ }
            else if(n>=16&&eq(raw+val,"application/json",16)) { /* json content */ }
            else if(n>=21&&eq(raw+val,"application/xhtml+xml",21)) { /* xhtml content */ }
            else if(n>=15&&eq(raw+val,"application/xml",15)) { /* xml content */ }
        }
        pos+=2;
    }
    if(!headers_done||pos>size) return 0;
    if(chunked) {
        if(has_length) { *error="Çelişkili HTTP uzunluk bilgisi."; return 0; }
        unsigned written=0;
        for(unsigned chunks=0;chunks<65536;chunks++) {
            unsigned value=0,digits=0;
            while(pos<size) {
                int c=lower(raw[pos]),v=c>='0'&&c<='9'?c-'0':(c>='a'&&c<='f'?c-'a'+10:-1);
                if(v<0) break;
                if(++digits>8||value>cap/16) return 0;
                value=value*16+v; pos++;
            }
            if(!digits) return 0;
            if(pos<size&&raw[pos]==';') while(pos<size&&raw[pos]!='\r') pos++;
            if(pos+2>size||raw[pos]!='\r'||raw[pos+1]!='\n') return 0; pos+=2;
            if(!value) { body[written]=0; *body_size=written; *error=""; return 1; }
            if(value>=cap-written||value>size-pos) return 0;
            kmemcpy(body+written,raw+pos,value); written+=value; pos+=value;
            if(pos+2>size||raw[pos]!='\r'||raw[pos+1]!='\n') return 0; pos+=2;
        }
        return 0;
    }
    unsigned remaining=size-pos;
    if(has_length&&length>remaining) { *error="Sayfa eksik indirildi."; return 0; }
    if(has_length) remaining=length;
    if(remaining>=cap) { *error="Sayfa boyut sınırını aşıyor."; return 0; }
    kmemcpy(body,raw+pos,remaining); body[remaining]=0; *body_size=remaining; *error=""; return 1;
}
int web_http_decode(const char *raw,unsigned size,char *body,unsigned cap,
                    unsigned *body_size,int *status,char *location,const char **error) {
    int media;
    if(!web_response_decode(raw,size,body,cap,body_size,status,location,error,&media)) return 0;
    if(media) { *error="Metin yanıtı bekleniyor."; return 0; }
    return 1;
}
int web_text_utf8(const char *raw,unsigned raw_size,char *body,unsigned *size,unsigned cap) {
    unsigned end=0; while(end+3<raw_size&&end<8192&&kmemcmp(raw+end,"\r\n\r\n",4)) end++;
    int latin=0,turkish=0;
    for(unsigned i=0;i+8<end;i++) if(eq(raw+i,"charset=",8)) {
        unsigned at=i+8; if(raw[at]=='\"'||raw[at]=='\'') at++;
        if(at+10<=end&&eq(raw+at,"iso-8859-",9)&&(raw[at+9]=='1'||raw[at+9]=='9')) {
            latin=1; turkish=raw[at+9]=='9';
        }
        break;
    }
    if(!latin) return 1;
    unsigned n=*size,needed=n;
    for(unsigned i=0;i<n;i++) if((uint8_t)body[i]>=128) needed++;
    if(needed>=cap) return 0;
    unsigned at=needed; body[needed]=0;
    while(n) {
        unsigned c=(uint8_t)body[--n];
        if(turkish) {
            if(c==0xd0) c=0x11e; else if(c==0xdd) c=0x130; else if(c==0xde) c=0x15e;
            else if(c==0xf0) c=0x11f; else if(c==0xfd) c=0x131; else if(c==0xfe) c=0x15f;
        }
        if(c>=128) { body[--at]=0x80|(c&63); body[--at]=0xc0|(c>>6); }
        else body[--at]=c;
    }
    *size=needed; return 1;
}
static void newline(web_page_t *p,int link,int heading) {
    if(p->count>=WEB_LINES) { p->truncated=1; return; }
    web_line_t *line=&p->lines[p->count-1];
    if(line->text[0]) { if(p->count==WEB_LINES) { p->truncated=1; return; } p->count++; }
    line=&p->lines[p->count-1]; line->link=link; line->heading=heading;
}
static void append(web_page_t *p,const char *bytes,unsigned len,unsigned columns,int link,int heading) {
    if(p->truncated) return;
    web_line_t *line=&p->lines[p->count-1]; unsigned used=kstrlen(line->text);
    if(len==1&&bytes[0]==' '&&(!used||line->text[used-1]==' ')) return;
    if(used+len>=columns) {
        /* Prefer wrapping at the last word boundary. */
        unsigned split=used; while(split&&line->text[split-1]!=' ') split--;
        char tail[100]; tail[0]=0;
        if(split>columns/2) { kstrcpy(tail,line->text+split); line->text[split-1]=0; }
        newline(p,link,heading); if(p->truncated) return;
        line=&p->lines[p->count-1]; kstrcpy(line->text,tail); used=kstrlen(tail);
    }
    if(used+len>=sizeof(line->text)) return;
    kmemcpy(line->text+used,bytes,len); line->text[used+len]=0;
}
static unsigned entity(const char *s,unsigned n,char *out,unsigned *size) {
    static const char *names[]={"amp;","lt;","gt;","quot;","apos;","nbsp;"};
    static const char values[]={'&','<','>','"','\'',' '};
    for(unsigned i=0;i<6;i++) { unsigned len=kstrlen(names[i]); if(n>=len&&eq(s,names[i],len)) { *out=values[i]; *size=1; return len; } }
    if(n&&s[0]=='#') {
        unsigned at=1,value=0,base=10;
        if(at<n&&(s[at]=='x'||s[at]=='X')) { at++; base=16; }
        unsigned begin=at;
        while(at<n&&at<10&&s[at]!=';') {
            int c=lower(s[at]),v=c>='0'&&c<='9'?c-'0':(base==16&&c>='a'&&c<='f'?c-'a'+10:-1);
            if(v<0||value>65535) return 0; value=value*base+v; at++;
        }
        if(at==begin||at>=n||s[at]!=';') return 0;
        if(value>=32&&value<127) { out[0]=value; *size=1; }
        else if(value>=128&&value<2048) { out[0]=0xc0|(value>>6); out[1]=0x80|(value&63); *size=2; }
        else { out[0]=' '; *size=1; }
        return at+1;
    }
    return 0;
}
static void href_read(const char *tag,unsigned size,char *out) {
    out[0]=0;
    for(unsigned i=0;i+4<size;i++) if((i==0||tag[i-1]==' '||tag[i-1]=='\t'||tag[i-1]=='\n')&&eq(tag+i,"href",4)) {
        i+=4; while(i<size&&(tag[i]==' '||tag[i]=='\t')) i++;
        if(i>=size||tag[i++]!='=') return;
        while(i<size&&(tag[i]==' '||tag[i]=='\t')) i++;
        char quote=(i<size&&(tag[i]=='\''||tag[i]=='"'))?tag[i++]:0;
        unsigned written=0;
        while(i<size&&(quote?tag[i]!=quote:(tag[i]!=' '&&tag[i]!='\t'&&tag[i]!='>'))) {
            char value[2]; unsigned bytes=1,consumed=0; value[0]=tag[i];
            if(tag[i]=='&') consumed=entity(tag+i+1,size-i-1,value,&bytes);
            if(written+bytes>=WEB_URL_MAX) { out[0]=0; return; }
            kmemcpy(out+written,value,bytes); written+=bytes; i+=consumed?consumed+1:1;
        }
        out[written]=0; return;
    }
}
void web_page_parse(const char *body,unsigned size,web_page_t *page,unsigned columns) {
    kmemset(page,0,sizeof(*page)); page->count=1; page->lines[0].link=-1;
    if(columns<30) columns=30; if(columns>99) columns=99;
    int skip=0,title=0,link=-1,heading=0;
    unsigned title_len=0;
    for(unsigned i=0;i<size&&!page->truncated;) {
        if(body[i]=='<') {
            if(i+4<size&&!kmemcmp(body+i,"<!--",4)) {
                i+=4; while(i+3<size&&kmemcmp(body+i,"-->",3)) i++; i=i+3<size?i+3:size; continue;
            }
            unsigned begin=++i; int close=0; if(i<size&&body[i]=='/') { close=1; i++; }
            char name[16]; unsigned n=0;
            while(i<size&&((body[i]>='a'&&body[i]<='z')||(body[i]>='A'&&body[i]<='Z')||(body[i]>='0'&&body[i]<='9'))) {
                if(n<15) name[n++]=lower(body[i]); i++;
            }
            name[n]=0; char quote=0;
            while(i<size) { char c=body[i]; if(!quote&&c=='>') break; if(c=='\''||c=='"') { if(!quote) quote=c; else if(quote==c) quote=0; } i++; }
            unsigned end=i; if(i<size) i++;
            if(!kstrcmp(name,"script")||!kstrcmp(name,"style")||!kstrcmp(name,"head")) {
                if(close) skip=0; else skip=1; continue;
            }
            if(!kstrcmp(name,"title")) { title=!close; continue; }
            if(skip) continue;
            if(!kstrcmp(name,"a")) {
                newline(page,-1,0); link=-1;
                if(!close&&page->link_count<WEB_LINKS) {
                    href_read(body+begin,end-begin,page->links[page->link_count].url);
                    if(page->links[page->link_count].url[0]) link=page->link_count++;
                }
                page->lines[page->count-1].link=link;
            } else if(!kstrcmp(name,"h1")||!kstrcmp(name,"h2")||!kstrcmp(name,"h3")) {
                newline(page,link,0); heading=!close; page->lines[page->count-1].heading=heading;
            } else if(!kstrcmp(name,"p")||!kstrcmp(name,"div")||!kstrcmp(name,"br")||!kstrcmp(name,"li")||!kstrcmp(name,"tr")||!kstrcmp(name,"hr")) newline(page,link,heading);
            else if(!kstrcmp(name,"td")) append(page," ",1,columns,link,heading);
            continue;
        }
        char value[2]; unsigned bytes=1,consumed=0; value[0]=body[i];
        if(body[i]=='&') consumed=entity(body+i+1,size-i-1,value,&bytes);
        if(!consumed&&(uint8_t)body[i]>=0xc0&&(uint8_t)body[i]<0xe0&&i+1<size&&((uint8_t)body[i+1]&0xc0)==0x80) { value[1]=body[i+1]; bytes=2; consumed=1; }
        i+=consumed?consumed+1:1;
        if(bytes==1&&(uint8_t)value[0]<32) value[0]=' ';
        if(title) { if(title_len+bytes<sizeof(page->title)) { kmemcpy(page->title+title_len,value,bytes); title_len+=bytes; page->title[title_len]=0; } continue; }
        if(skip) continue;
        append(page,value,bytes,columns,link,heading);
    }
    if(!page->title[0]) kstrcpy(page->title,"Web sayfası");
}
