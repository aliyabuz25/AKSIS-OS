#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <lexbor/html/html.h>
#include <lexbor/style/style.h>
#include <lexbor/selectors/selectors.h>
#include "quickjs.h"
#include "bootstrap.h"

#define HANDLES 4096
#define TIMERS 64
#define RESOURCE_LIMIT (1024ul*1024)
typedef struct { unsigned id; unsigned long long due, interval; JSValue fn; } aw_timer;
struct aw_view {
    aw_host host;
    lxb_html_document_t *doc;
    JSRuntime *rt;
    JSContext *ctx;
    lxb_dom_node_t *handles[HANDLES];
    unsigned count, timer_id;
    aw_timer timers[TIMERS];
    unsigned long long deadline;
    unsigned instructions;
    int dirty, scripts;
    char url[256], error[256];
};
typedef struct { char *p; size_t n, cap; } buffer;
static lxb_status_t append(const lxb_char_t *s, size_t n, void *opaque) {
    buffer *b=opaque;
    if (n >= b->cap-b->n) return LXB_STATUS_ERROR_SMALL_BUFFER;
    memcpy(b->p+b->n,s,n); b->n+=n; b->p[b->n]=0; return LXB_STATUS_OK;
}
static void copy(char *out, unsigned cap, const void *s, size_t n) {
    if (!cap) return;
    if (!s) n=0;
    if (n>=cap) { n=cap-1; while(n && (((const unsigned char *)s)[n]&0xc0)==0x80) n--; }
    if(n) memcpy(out,s,n); out[n]=0;
}
static void fail(aw_view *v, const char *s) {
    copy(v->error,sizeof(v->error),s,strlen(s));
    if(v->host.log) v->host.log(v->host.opaque,v->error);
}
const char *aw_error(const aw_view *v) { return v?v->error:"Web runtime unavailable"; }
static unsigned long long now(aw_view *v) { return v->host.now?v->host.now(v->host.opaque):0; }
static void budget(aw_view *v) { v->deadline=now(v)+50; v->instructions=2000; }
static int interrupt(JSRuntime *rt, void *opaque) {
    (void)rt; aw_view *v=opaque;
    return !v->instructions-- || now(v)>v->deadline;
}
static int exception(aw_view *v, JSValue value) {
    if (!JS_IsException(value)) return 0;
    JSValue error=JS_GetException(v->ctx);
    const char *s=JS_ToCString(v->ctx,error);
    fail(v,s?s:"JavaScript exception");
    if(s) JS_FreeCString(v->ctx,s); JS_FreeValue(v->ctx,error); return 1;
}
int aw_resolve(const char *base, const char *ref, char *out, unsigned cap) {
    char raw[512];
    if(!ref||!base||!cap) return 0;
    if(!strncmp(ref,"file://",7)) ref+=7;
    if(strchr(ref,':')||strchr(ref,'\\')||strchr(ref,'%')||!strncmp(ref,"//",2)) return 0;
    if(!strncmp(base,"file://",7)) base+=7;
    if(*ref=='/') { if(strlen(ref)>=sizeof(raw)) return 0; strcpy(raw,ref); }
    else {
        if(*base!='/') return 0;
        const char *slash=strrchr(base,'/'); size_t dir=slash?(size_t)(slash-base+1):1;
        if(dir+strlen(ref)>=sizeof(raw)) return 0;
        memcpy(raw,base,dir); strcpy(raw+dir,ref);
    }
    unsigned n=1; if(cap<2) return 0; out[0]='/'; out[1]=0;
    for(char *p=raw; *p && *p!='?' && *p!='#';) {
        while(*p=='/') p++;
        char *start=p; while(*p&&*p!='/'&&*p!='?'&&*p!='#') p++;
        size_t len=p-start;
        if(!len) break;
        if(len==1&&*start=='.') continue;
        if(len==2&&!memcmp(start,"..",2)) { if(n==1) return 0; while(n>1&&out[n-1]!='/') n--; if(n>1)n--; out[n]=0; continue; }
        if(n+len+2>cap) return 0;
        if(n>1)out[n++]='/'; memcpy(out+n,start,len); n+=len; out[n]=0;
    }
    return 1;
}
static const char *resource(aw_view *v,const char *base,const char *ref, char path[256], unsigned long *size) {
    if(!v->host.read || !aw_resolve(base,ref,path,256)) { fail(v,"Unsupported resource URL"); return NULL; }
    const char *s=v->host.read(v->host.opaque,path,size);
    if(!s||*size>RESOURCE_LIMIT) { fail(v,"Resource missing or exceeds 1 MiB limit"); return NULL; }
    return s;
}
static int handle(aw_view *v,lxb_dom_node_t *n) {
    if(!n)return 0;
    for(unsigned i=1;i<=v->count;i++)if(v->handles[i]==n)return i;
    if(v->count+1>=HANDLES) { fail(v,"DOM handle limit reached"); return 0; }
    v->handles[++v->count]=n; return v->count;
}
static const char *attr(lxb_dom_node_t *n,const char *key,size_t *len) {
    *len=0; if(!n||n->type!=LXB_DOM_NODE_TYPE_ELEMENT)return NULL;
    return (const char *)lxb_dom_element_get_attribute(lxb_dom_interface_element(n),(const lxb_char_t *)key,strlen(key),len);
}
static void attr_copy(lxb_dom_node_t *n,const char *key,char *out,unsigned cap) {
    size_t len; const char *s=attr(n,key,&len); copy(out,cap,s,len);
}
static int style(lxb_dom_node_t *node,const char *key,char *out,unsigned cap) {
    out[0]=0; if(node->type!=LXB_DOM_NODE_TYPE_ELEMENT)return 0;
    const lxb_css_rule_declaration_t *d=lxb_html_element_style_by_name(lxb_html_interface_element(node),(const lxb_char_t *)key,strlen(key));
    if(!d)return 0;
    char temp[512]; buffer b={temp,0,sizeof(temp)}; temp[0]=0;
    if(lxb_css_rule_declaration_serialize(d,append,&b)!=LXB_STATUS_OK)return 0;
    char *p=strchr(temp,':'); if(!p)return 0; p++; while(isspace((unsigned char)*p))p++;
    copy(out,cap,p,strlen(p)); char *important=strstr(out,"!important"); if(important)*important=0;
    size_t len=strlen(out); while(len&&(isspace((unsigned char)out[len-1])||out[len-1]==';'))out[--len]=0;
    return 1;
}
typedef struct {aw_view *v;JSValue array;unsigned index;} query_ctx;
static lxb_status_t found(lxb_dom_node_t *n,lxb_css_selector_specificity_t spec,void *opaque) {
    (void)spec; query_ctx *q=opaque; int id=handle(q->v,n);
    if(!id||JS_SetPropertyUint32(q->v->ctx,q->array,q->index++,JS_NewInt32(q->v->ctx,id))<0)return LXB_STATUS_STOP;
    return LXB_STATUS_OK;
}
static JSValue query(aw_view *v,lxb_dom_node_t *root,const char *selector) {
    lxb_css_parser_t *p=lxb_css_parser_create(); lxb_selectors_t *s=lxb_selectors_create();
    if(!p||!s||lxb_css_parser_init(p,NULL)||lxb_selectors_init(s)) {
        if(p)lxb_css_parser_destroy(p,true); if(s)lxb_selectors_destroy(s,true); return JS_ThrowOutOfMemory(v->ctx);
    }
    lxb_css_selector_list_t *list=lxb_css_selectors_parse(p,(const lxb_char_t *)selector,strlen(selector));
    query_ctx q={v,JS_NewArray(v->ctx),0};
    if(p->status!=LXB_STATUS_OK||!list) { JS_FreeValue(v->ctx,q.array); q.array=JS_ThrowSyntaxError(v->ctx,"Invalid selector"); }
    else lxb_selectors_find(s,root,list,found,&q);
    lxb_selectors_destroy(s,true); lxb_css_parser_destroy(p,true);
    if(list)lxb_css_selector_list_destroy_memory(list);
    return q.array;
}
static JSValue dom(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self; aw_view *v=JS_GetContextOpaque(ctx); int id=0;
    if(argc<2||JS_ToInt32(ctx,&id,argv[1]))return JS_EXCEPTION;
    const char *op=JS_ToCString(ctx,argv[0]); if(!op)return JS_EXCEPTION;
    const char *a=NULL,*b=NULL; size_t alen=0,blen=0;
    JSValue result=JS_UNDEFINED;
    lxb_dom_node_t *n=id>0&&(unsigned)id<=v->count?v->handles[id]:NULL;
    if(!strcmp(op,"document")) { result=JS_NewInt32(ctx,handle(v,lxb_dom_interface_node(v->doc))); goto done; }
    if(!strcmp(op,"body")||!strcmp(op,"head")) { result=JS_NewInt32(ctx,handle(v,!strcmp(op,"body")?lxb_dom_interface_node(v->doc->body):lxb_dom_interface_node(v->doc->head)));goto done; }
    if(!strcmp(op,"fragment")) { result=JS_NewInt32(ctx,handle(v,lxb_dom_interface_node(lxb_dom_document_create_document_fragment(&v->doc->dom_document))));goto done; }
    if(argc>2) { a=JS_ToCStringLen(ctx,&alen,argv[2]); if(!a){result=JS_EXCEPTION;goto done;} }
    if(argc>3) { b=JS_ToCStringLen(ctx,&blen,argv[3]); if(!b){result=JS_EXCEPTION;goto done;} }
    if(!strcmp(op,"create")||!strcmp(op,"createText")) {
        if(!a){result=JS_ThrowTypeError(ctx,"Missing node content");goto done;}
        lxb_dom_node_t *created=!strcmp(op,"create")?lxb_dom_interface_node(lxb_dom_document_create_element(&v->doc->dom_document,(const lxb_char_t *)a,alen,NULL)):lxb_dom_interface_node(lxb_dom_document_create_text_node(&v->doc->dom_document,(const lxb_char_t *)a,alen));
        int hid=handle(v,created); result=hid?JS_NewInt32(ctx,hid):JS_ThrowOutOfMemory(ctx);goto done;
    }
    if(!strcmp(op,"title")) {
        if(a){lxb_html_document_title_set(v->doc,(const lxb_char_t *)a,alen);v->dirty=1;}
        else {size_t len=0;const lxb_char_t *s=lxb_html_document_title(v->doc,&len);result=JS_NewStringLen(ctx,(const char *)(s?s:(const lxb_char_t *)""),len);}goto done;
    }
    if(!strcmp(op,"getId")) {
        lxb_dom_element_t *el=a?lxb_dom_element_by_id(lxb_dom_interface_element(v->doc->dom_document.element),(const lxb_char_t *)a,alen):NULL;
        result=JS_NewInt32(ctx,handle(v,lxb_dom_interface_node(el)));goto done;
    }
    if(!n){result=JS_ThrowTypeError(ctx,"Invalid DOM handle");goto done;}
    if(!strcmp(op,"type"))result=JS_NewInt32(ctx,n->type);
    else if(!strcmp(op,"parent"))result=JS_NewInt32(ctx,handle(v,n->parent));
    else if(!strcmp(op,"next"))result=JS_NewInt32(ctx,handle(v,n->next));
    else if(!strcmp(op,"tag")){size_t len=0;const lxb_char_t *s=lxb_dom_node_name(n,&len);result=JS_NewStringLen(ctx,(const char *)s,len);}
    else if(!strcmp(op,"children")){result=JS_NewArray(ctx);unsigned i=0;for(lxb_dom_node_t *c=n->first_child;c;c=c->next)JS_SetPropertyUint32(ctx,result,i++,JS_NewInt32(ctx,handle(v,c)));}
    else if(!strcmp(op,"query"))result=a?query(v,n,a):JS_ThrowTypeError(ctx,"Missing selector");
    else if(!strcmp(op,"text")||!strcmp(op,"html")) {
        if(a) {
            if(n->type==LXB_DOM_NODE_TYPE_TEXT)lxb_dom_node_text_content_set(n,(const lxb_char_t *)a,alen);
            else {
                while(n->first_child)lxb_dom_node_remove(n->first_child);
                if(!strcmp(op,"html")&&n->type==LXB_DOM_NODE_TYPE_ELEMENT){
                    lxb_dom_node_t *frag=lxb_html_document_parse_fragment(v->doc,lxb_dom_interface_element(n),(const lxb_char_t *)a,alen);
                    if(!frag){result=JS_ThrowOutOfMemory(ctx);goto done;}
                    while(frag->first_child){lxb_dom_node_t *c=frag->first_child;lxb_dom_node_remove(c);lxb_dom_node_insert_child(n,c);}
                    lxb_dom_node_destroy(frag);
                } else if(alen){lxb_dom_node_t *t=lxb_dom_interface_node(lxb_dom_document_create_text_node(&v->doc->dom_document,(const lxb_char_t *)a,alen));if(!t){result=JS_ThrowOutOfMemory(ctx);goto done;}lxb_dom_node_insert_child(n,t);}
            }
            v->dirty=1;
        } else if(!strcmp(op,"html")) {
            char *out=malloc(RESOURCE_LIMIT);if(!out){result=JS_ThrowOutOfMemory(ctx);goto done;}
            buffer buf={out,0,RESOURCE_LIMIT};out[0]=0;
            if(lxb_html_serialize_deep_cb(n,append,&buf))result=JS_ThrowRangeError(ctx,"HTML serialization limit");
            else result=JS_NewStringLen(ctx,out,buf.n);free(out);
        } else {size_t len=0;lxb_char_t *s=lxb_dom_node_text_content(n,&len);result=s?JS_NewStringLen(ctx,(char *)s,len):JS_NewString(ctx,"");if(s)lxb_dom_document_destroy_text(n->owner_document,s);}
    } else if(!strcmp(op,"append")||!strcmp(op,"insert")||!strcmp(op,"remove")) {
        int cid=0,bid=0; if(argc<3||JS_ToInt32(ctx,&cid,argv[2])){result=JS_EXCEPTION;goto done;}
        if(argc>3&&JS_ToInt32(ctx,&bid,argv[3])){result=JS_EXCEPTION;goto done;}
        lxb_dom_node_t *child=cid>0&&(unsigned)cid<=v->count?v->handles[cid]:NULL;
        lxb_dom_node_t *before=bid>0&&(unsigned)bid<=v->count?v->handles[bid]:NULL;
        if(!child||(!strcmp(op,"remove")?child->parent!=n:lxb_dom_node_pre_insert_validity(n,child,before)!=LXB_DOM_EXCEPTION_OK)){result=JS_ThrowTypeError(ctx,"Invalid DOM hierarchy");goto done;}
        if(!strcmp(op,"remove"))lxb_dom_node_remove(child);
        else {lxb_dom_node_remove(child);if(before)lxb_dom_node_insert_before(before,child);else lxb_dom_node_insert_child(n,child);}
        v->dirty=1;
    } else if(n->type==LXB_DOM_NODE_TYPE_ELEMENT&&a) {
        lxb_dom_element_t *el=lxb_dom_interface_element(n);
        if(!strcmp(op,"getAttr")){size_t len;const char *s=attr(n,a,&len);result=s?JS_NewStringLen(ctx,s,len):JS_NULL;}
        else if(!strcmp(op,"setAttr")&&b){if(!lxb_dom_element_set_attribute(el,(const lxb_char_t *)a,alen,(const lxb_char_t *)b,blen))result=JS_ThrowOutOfMemory(ctx);v->dirty=1;}
        else if(!strcmp(op,"removeAttr")){lxb_dom_element_remove_attribute(el,(const lxb_char_t *)a,alen);v->dirty=1;}
        else if(!strcmp(op,"style")){char value[512];style(n,a,value,sizeof(value));result=JS_NewString(ctx,value);}
    }
done:
    if(a)JS_FreeCString(ctx,a);if(b)JS_FreeCString(ctx,b);JS_FreeCString(ctx,op);return result;
}
static JSValue log_js(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv) {
    (void)self;aw_view *v=JS_GetContextOpaque(ctx);char line[512]="";size_t n=0;
    for(int i=0;i<argc;i++){const char *s=JS_ToCString(ctx,argv[i]);if(!s)return JS_EXCEPTION;size_t len=strlen(s);if(i&&n+1<sizeof(line))line[n++]=' ';if(len>sizeof(line)-n-1)len=sizeof(line)-n-1;memcpy(line+n,s,len);n+=len;JS_FreeCString(ctx,s);}line[n]=0;
    if(v->host.log)v->host.log(v->host.opaque,line);return JS_UNDEFINED;
}
static JSValue performance_now(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){(void)self;(void)argc;(void)argv;return JS_NewFloat64(ctx,(double)now(JS_GetContextOpaque(ctx)));}
static JSValue timer_js(JSContext *ctx,JSValueConst self,int argc,JSValueConst *argv){
    (void)self;aw_view *v=JS_GetContextOpaque(ctx);double ms=0;if(argc<2||JS_ToFloat64(ctx,&ms,argv[1]))return JS_EXCEPTION;
    if(JS_IsNull(argv[0])){for(unsigned i=0;i<TIMERS;i++)if(v->timers[i].id&&(double)v->timers[i].id==ms){JS_FreeValue(ctx,v->timers[i].fn);memset(&v->timers[i],0,sizeof(aw_timer));}return JS_UNDEFINED;}
    if(!JS_IsFunction(ctx,argv[0]))return JS_ThrowTypeError(ctx,"Timer requires a function");
    if(!isfinite(ms)||ms<1)ms=1;if(ms>2147483647)ms=2147483647;
    for(unsigned i=0;i<TIMERS;i++)if(!v->timers[i].id){aw_timer *t=&v->timers[i];t->id=++v->timer_id;if(!t->id)t->id=++v->timer_id;t->due=now(v)+(unsigned)ms;t->interval=argc>2&&JS_ToBool(ctx,argv[2])?(unsigned)ms:0;t->fn=JS_DupValue(ctx,argv[0]);return JS_NewUint32(ctx,t->id);}
    return JS_ThrowRangeError(ctx,"Timer limit reached");
}
static char *normalize_module(JSContext *ctx,const char *base,const char *name,void *opaque){
    aw_view *v=opaque;char path[256];if(!aw_resolve(base[0]=='/'?base:v->url,name,path,sizeof(path))){JS_ThrowReferenceError(ctx,"Unsupported module URL");return NULL;}return js_strdup(ctx,path);
}
static JSModuleDef *load_module(JSContext *ctx,const char *name,void *opaque){
    aw_view *v=opaque;char path[256];unsigned long size=0;const char *s=resource(v,v->url,name,path,&size);if(!s){JS_ThrowReferenceError(ctx,"Cannot load module: %s",name);return NULL;}
    JSValue val=JS_Eval(ctx,s,size,path,JS_EVAL_TYPE_MODULE|JS_EVAL_FLAG_COMPILE_ONLY);if(JS_IsException(val))return NULL;JSModuleDef *m=JS_VALUE_GET_PTR(val);JS_FreeValue(ctx,val);return m;
}
aw_view *aw_create(const aw_host *host){aw_view *v=calloc(1,sizeof(*v));if(v)v->host=*host;return v;}
static void unload(aw_view *v){
    if(v->ctx){for(unsigned i=0;i<TIMERS;i++)if(v->timers[i].id)JS_FreeValue(v->ctx,v->timers[i].fn);JS_FreeContext(v->ctx);}
    if(v->rt)JS_FreeRuntime(v->rt);
    if(v->doc){lxb_style_destroy(v->doc);lxb_html_document_destroy(v->doc);}
    v->rt=NULL;v->ctx=NULL;v->doc=NULL;v->count=0;v->timer_id=0;memset(v->handles,0,sizeof(v->handles));memset(v->timers,0,sizeof(v->timers));
}
void aw_destroy(aw_view *v){if(v){unload(v);free(v);}}
static int evaluate(aw_view *v,const char *src,size_t size,const char *name,int flags){budget(v);JSValue val=JS_Eval(v->ctx,src,size,name,flags);int ok=!exception(v,val);JS_FreeValue(v->ctx,val);return ok;}
int aw_eval(aw_view *v,const char *src,char *out,unsigned cap){
    if(out&&cap)out[0]=0;if(!v||!v->ctx)return 0;budget(v);JSValue val=JS_Eval(v->ctx,src,strlen(src),v->url,JS_EVAL_TYPE_GLOBAL);int ok=!exception(v,val);
    if(ok&&out&&cap){const char *s=JS_ToCString(v->ctx,val);if(s){copy(out,cap,s,strlen(s));JS_FreeCString(v->ctx,s);}else{exception(v,JS_EXCEPTION);ok=0;}}JS_FreeValue(v->ctx,val);return ok;
}
int aw_poll(aw_view *v){
    if(!v)return 0;
    if(v->ctx){budget(v);unsigned long long time=now(v);
        for(unsigned i=0;i<TIMERS&&now(v)<=v->deadline;i++){aw_timer *t=&v->timers[i];if(t->id&&t->due<=time){JSValue fn=JS_DupValue(v->ctx,t->fn);if(t->interval)t->due=time+t->interval;else{JS_FreeValue(v->ctx,t->fn);t->id=0;}JSValue ret=JS_Call(v->ctx,fn,JS_UNDEFINED,0,NULL);exception(v,ret);JS_FreeValue(v->ctx,ret);JS_FreeValue(v->ctx,fn);}}
        for(unsigned i=0;i<64&&now(v)<=v->deadline;i++){JSContext *ctx=NULL;int rc=JS_ExecutePendingJob(v->rt,&ctx);if(rc<=0){if(rc<0)exception(v,JS_EXCEPTION);break;}}
    }
    int dirty=v->dirty;v->dirty=0;return dirty;
}
static lxb_dom_node_t *next_node(lxb_dom_node_t *n,lxb_dom_node_t *root){if(n->first_child)return n->first_child;while(n!=root&&!n->next)n=n->parent;return n==root?NULL:n->next;}
int aw_load(aw_view *v,const char *url,const char *html,unsigned long size,int scripts){
    if(!v||!url||!html||size>RESOURCE_LIMIT||strlen(url)>=sizeof(v->url))return 0;
    unload(v);v->error[0]=0;strcpy(v->url,url);v->scripts=scripts;v->dirty=1;
    v->doc=lxb_html_document_create();if(!v->doc||lxb_style_init(v->doc)||lxb_html_document_parse(v->doc,(const lxb_char_t *)html,size)){fail(v,"HTML/CSS parser allocation failed");return 0;}
    lxb_dom_node_t *root=lxb_dom_interface_node(v->doc);
    /* Insert linked sheets at their DOM position, preserving cascade order. */
    unsigned resources=0;
    for(lxb_dom_node_t *n=root;n;n=next_node(n,root))if(n->local_name==LXB_TAG_LINK&&resources++<32){
        char rel[64],href[256],path[256];attr_copy(n,"rel",rel,sizeof(rel));attr_copy(n,"href",href,sizeof(href));
        if(strcmp(rel,"stylesheet")||!href[0])continue;
        unsigned long len=0;const char *css=resource(v,url,href,path,&len);if(!css)continue;
        lxb_dom_element_t *el=lxb_dom_document_create_element(&v->doc->dom_document,(const lxb_char_t *)"style",5,NULL);
        if(!el){fail(v,"Stylesheet allocation failed");continue;}
        lxb_dom_node_t *sn=lxb_dom_interface_node(el);lxb_dom_node_text_content_set(sn,(const lxb_char_t *)css,len);lxb_dom_node_insert_after(n,sn);
    }
    if(!scripts)return 1;
    v->rt=JS_NewRuntime();if(!v->rt){fail(v,"JavaScript runtime allocation failed");return 0;}
    JS_SetMemoryLimit(v->rt,8*1024*1024);JS_SetMaxStackSize(v->rt,128*1024);JS_SetInterruptHandler(v->rt,interrupt,v);JS_SetModuleLoaderFunc(v->rt,normalize_module,load_module,v);
    v->ctx=JS_NewContext(v->rt);if(!v->ctx){fail(v,"JavaScript context allocation failed");return 0;}JS_SetContextOpaque(v->ctx,v);
    JSValue global=JS_GetGlobalObject(v->ctx),console=JS_NewObject(v->ctx),perf=JS_NewObject(v->ctx);
    const char *levels[]={"log","info","warn","error","debug"};for(unsigned i=0;i<5;i++)JS_SetPropertyStr(v->ctx,console,levels[i],JS_NewCFunction(v->ctx,log_js,levels[i],1));
    JS_SetPropertyStr(v->ctx,global,"console",console);JS_SetPropertyStr(v->ctx,perf,"now",JS_NewCFunction(v->ctx,performance_now,"now",0));JS_SetPropertyStr(v->ctx,global,"performance",perf);
    JS_SetPropertyStr(v->ctx,global,"__axisDOM",JS_NewCFunction(v->ctx,dom,"__axisDOM",4));JS_SetPropertyStr(v->ctx,global,"__axisTimer",JS_NewCFunction(v->ctx,timer_js,"__axisTimer",3));JS_FreeValue(v->ctx,global);
    if(!evaluate(v,aw_bootstrap,sizeof(aw_bootstrap)-1,"axis:bootstrap",JS_EVAL_TYPE_GLOBAL))return 0;
    /* Snapshot script nodes so DOM mutation does not invalidate iteration. */
    lxb_dom_node_t *scripts_list[64];unsigned count=0;
    for(lxb_dom_node_t *n=root;n;n=next_node(n,root))if(n->local_name==LXB_TAG_SCRIPT&&count<64)scripts_list[count++]=n;
    for(unsigned i=0;i<count;i++){
        lxb_dom_node_t *n=scripts_list[i];char src[256],type[64],path[256];attr_copy(n,"src",src,sizeof(src));attr_copy(n,"type",type,sizeof(type));
        if(type[0]&&strcmp(type,"module")&&strcmp(type,"text/javascript")&&strcmp(type,"application/javascript"))continue;
        size_t len=0;lxb_char_t *inline_code=NULL;const char *code;
        if(src[0]){unsigned long sz=0;code=resource(v,url,src,path,&sz);len=sz;}
        else{inline_code=lxb_dom_node_text_content(n,&len);code=(const char *)inline_code;copy(path,sizeof(path),url,strlen(url));}
        if(code)evaluate(v,code,len,path,!strcmp(type,"module")?JS_EVAL_TYPE_MODULE:JS_EVAL_TYPE_GLOBAL);
        if(inline_code)lxb_dom_document_destroy_text(n->owner_document,inline_code);
    }
    evaluate(v,"__axisReady()",13,"axis:ready",JS_EVAL_TYPE_GLOBAL);aw_poll(v);v->dirty=1;return 1;
}
int aw_click(aw_view *v,int id){if(!v||!v->ctx||id<1||(unsigned)id>v->count)return 0;char src[64],out[16];snprintf(src,sizeof(src),"__axisClick(%d)",id);int ok=aw_eval(v,src,out,sizeof(out));aw_poll(v);return ok&&!strcmp(out,"true");}
static unsigned color_value(const char *s,unsigned fallback){
    if(!strcmp(s,"transparent"))return 0;
    if(!strcmp(s,"white"))return 0xffffffff;if(!strcmp(s,"black"))return 0xff000000;if(!strcmp(s,"red"))return 0xffff0000;if(!strcmp(s,"blue"))return 0xff0000ff;
    if(*s=='#'){unsigned long n=strtoul(s+1,NULL,16);size_t len=strlen(s+1);if(len==3)return 0xff000000|((n&0xf00)<<12)|((n&0xf00)<<8)|((n&0xf0)<<8)|((n&0xf0)<<4)|((n&0xf)<<4)|(n&0xf);if(len==6)return 0xff000000|n;}
    return fallback;
}
static int numeric(const char *s){int v=atoi(s);return v<0?0:v>32700?32700:v;}
static void computed(lxb_dom_node_t *n,aw_node *out){
    char s[512];
    if(style(n,"display",s,sizeof(s))){if(!strcmp(s,"none"))out->display=-1;else if(!strcmp(s,"flex")||!strcmp(s,"inline-flex"))out->display=1;}
    if(style(n,"flex-direction",s,sizeof(s))&&!strcmp(s,"column"))out->display=0;
    if(style(n,"color",s,sizeof(s))){out->color=color_value(s,out->color);out->flags|=AW_COLOR;}
    if(style(n,"background-color",s,sizeof(s))||style(n,"background",s,sizeof(s))){out->background=color_value(s,0);out->flags|=AW_BG;}
    if(style(n,"border-color",s,sizeof(s))){out->border=color_value(s,0);out->flags|=AW_BORDER;}
    if(style(n,"font-size",s,sizeof(s)))out->font_size=numeric(s);
    if(style(n,"font-weight",s,sizeof(s)))out->weight=!strcmp(s,"bold")?700:numeric(s);
    if(style(n,"text-align",s,sizeof(s)))out->align=!strcmp(s,"center")?1:!strcmp(s,"right")?2:0;
    if(style(n,"padding",s,sizeof(s))){out->padding_y=numeric(s);const char *p=strchr(s,' ');out->padding_x=p?numeric(p+1):out->padding_y;out->flags|=AW_PADDING;}
    if(style(n,"gap",s,sizeof(s))){out->gap=numeric(s);out->flags|=AW_GAP;}
    if(style(n,"margin-bottom",s,sizeof(s))){out->margin=numeric(s);out->flags|=AW_MARGIN;}
    if(style(n,"width",s,sizeof(s))&&!strchr(s,'%')&&strstr(s,"px"))out->width=numeric(s);
    if(style(n,"min-height",s,sizeof(s)))out->min_height=numeric(s);
    if(style(n,"border-radius",s,sizeof(s)))out->radius=numeric(s);
}
static int leaf(lxb_dom_node_t *n){return n->local_name==LXB_TAG_P||n->local_name==LXB_TAG_H1||n->local_name==LXB_TAG_H2||n->local_name==LXB_TAG_H3||n->local_name==LXB_TAG_A||n->local_name==LXB_TAG_BUTTON||n->local_name==LXB_TAG_TD||n->local_name==LXB_TAG_TH||n->local_name==LXB_TAG_LABEL||n->local_name==LXB_TAG_SPAN;}
static void export_node(aw_view *v,lxb_dom_node_t *n,int parent,aw_node *out,unsigned cap,int *count,unsigned depth){
    if(depth>128||*count>=(int)cap)return;
    if(n->type!=LXB_DOM_NODE_TYPE_ELEMENT&&n->type!=LXB_DOM_NODE_TYPE_TEXT)return;
    if(n->local_name==LXB_TAG_HEAD||n->local_name==LXB_TAG_STYLE||n->local_name==LXB_TAG_SCRIPT||n->local_name==LXB_TAG_LINK||n->local_name==LXB_TAG_META)return;
    if(n->local_name==LXB_TAG_TBODY||n->local_name==LXB_TAG_THEAD||n->local_name==LXB_TAG_TFOOT){for(lxb_dom_node_t *c=n->first_child;c;c=c->next)export_node(v,c,parent,out,cap,count,depth+1);return;}
    aw_node node={.parent=parent,.display=0,.color=parent>=0?out[parent].color:0xff1a1a1a,.font_size=parent>=0?out[parent].font_size:14};
    size_t len=0;const lxb_char_t *name=lxb_dom_node_name(n,&len);copy(node.tag,sizeof(node.tag),name,len);
    attr_copy(n,"class",node.classes,sizeof(node.classes));attr_copy(n,"id",node.id,sizeof(node.id));attr_copy(n,"href",node.href,sizeof(node.href));computed(n,&node);
    if(node.display<0||attr(n,"hidden",&len))return;
    int is_leaf=leaf(n)||n->type==LXB_DOM_NODE_TYPE_TEXT;
    if(is_leaf){lxb_char_t *s=lxb_dom_node_text_content(n,&len);if(s){unsigned used=0;int space=0;for(size_t i=0;i<len&&used+1<sizeof(node.text);i++){unsigned char c=s[i];if(c==' '||c=='\n'||c=='\t'||c=='\r'){space=used>0;continue;}if(space&&used+1<sizeof(node.text)){node.text[used++]=' ';space=0;}if(used+1<sizeof(node.text))node.text[used++]=c;}node.text[used]=0;lxb_dom_document_destroy_text(n->owner_document,s);}if(n->type==LXB_DOM_NODE_TYPE_TEXT&&!node.text[0])return;}
    node.handle=handle(v,n);int index=(*count)++;out[index]=node;
    if(!is_leaf)for(lxb_dom_node_t *c=n->first_child;c;c=c->next)export_node(v,c,index,out,cap,count,depth+1);
}
int aw_export(aw_view *v,aw_node *out,unsigned cap){if(!v||!v->doc||!v->doc->body||!cap)return 0;int count=0;export_node(v,lxb_dom_interface_node(v->doc->body),-1,out,cap,&count,0);if(count==(int)cap)fail(v,"Layout node limit reached");return count;}
