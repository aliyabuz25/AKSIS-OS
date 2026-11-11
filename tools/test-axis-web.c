#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../kernel/axis_web/runtime.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static unsigned long long time_ms;
static unsigned long long now(void *p){(void)p;return time_ms;}
static void log_message(void *p,const char *s){(void)p;fprintf(stderr,"runtime: %s\n",s);}
static const char *read_resource(void *p,const char *name,unsigned long *size){
    (void)p;const char *s=NULL;
    if(!strcmp(name,"/ui/theme.css"))s="p {color:#123456} #status {color:#0067c0 !important} .gone {display:none} .layout {display:flex;gap:18px}";
    if(!strcmp(name,"/ui/main.js"))s="document.getElementById('status').textContent='External JS'; document.querySelector('button').addEventListener('click',()=>{document.getElementById('status').textContent='Clicked';}); globalThis.loaded=true;";
    if(!strcmp(name,"/ui/answer.js"))s="export const answer=42;";
    if(!strcmp(name,"/ui/start.js"))s="import {answer} from './answer.js';globalThis.moduleAnswer=answer;";
    if(s)*size=strlen(s);return s;
}
static aw_node nodes[512];
static aw_node *by_id(int count,const char *id){for(int i=0;i<count;i++)if(!strcmp(nodes[i].id,id))return &nodes[i];return NULL;}
extern void *axis_malloc(unsigned long);
extern void *axis_realloc(void *,unsigned long);
extern void *axis_calloc(unsigned long,unsigned long);
extern void axis_free(void *);
int main(void){
    unsigned char arena[65536];axis_memory_init(arena,sizeof(arena));
    void *a=axis_malloc(100),*b=axis_malloc(200),*c=axis_malloc(300);CHECK(a&&b&&c&&((uintptr_t)a%16)==0);memset(a,0x5a,100);
    axis_free(b);a=axis_realloc(a,240);CHECK(a&&((unsigned char *)a)[99]==0x5a);axis_free(c);axis_free(a);CHECK(axis_memory_live()==0);
    a=axis_malloc(64000);CHECK(a);CHECK(!axis_malloc(64000));axis_free(a);CHECK(!axis_calloc(SIZE_MAX,2));
    char path[256];CHECK(aw_resolve("/ui/pages/main.html","../main.js",path,sizeof(path))&&!strcmp(path,"/ui/main.js"));
    CHECK(!aw_resolve("/ui/main.html","../../escape",path,sizeof(path)));CHECK(!aw_resolve("/ui/main.html","https://x/a",path,sizeof(path)));CHECK(!aw_resolve("/ui/main.html","x",path,3));
    aw_host host={.read=read_resource,.now=now,.log=log_message};aw_view *v=aw_create(&host),*second=aw_create(&host);CHECK(v&&second);
    const char *html="<!doctype html><html><head><title>Test</title><link rel='stylesheet' href='theme.css'></head><body><div class='layout'><p id='status' style='color:#ff0000'>Hello &amp; world</p><button id='go'>Run</button></div><p id='hidden' class='gone'>Invisible</p><table><tr><td>Cell</td></tr></table><script src='main.js'></script><script type='module' src='start.js'></script></body></html>";
    CHECK(aw_load(v,"/ui/index.html",html,strlen(html),1));
    char out[512];CHECK(aw_eval(v,"loaded && moduleAnswer === 42",out,sizeof(out))&&!strcmp(out,"true"));
    int n=aw_export(v,nodes,512);aw_node *status=by_id(n,"status"),*go=by_id(n,"go");CHECK(status&&go);
    CHECK(!strcmp(status->text,"External JS"));CHECK(status->color==0xff0067c0);CHECK(!by_id(n,"hidden"));
    CHECK(!aw_click(v,go->handle));n=aw_export(v,nodes,512);CHECK(!strcmp(by_id(n,"status")->text,"Clicked"));
    CHECK(aw_eval(v,"document.getElementById('go').addEventListener('click', e=>e.preventDefault())",out,sizeof(out)));
    CHECK(aw_click(v,by_id(n,"go")->handle));
    CHECK(aw_eval(v,"let el=document.createElement('p');el.id='created';el.append('A & B');document.body.appendChild(el);el.textContent",out,sizeof(out))&&!strcmp(out,"A & B"));
    CHECK(aw_eval(v,"let child=document.createElement('span');child.textContent='survives';el.appendChild(child);el.textContent='replaced';child.textContent",out,sizeof(out))&&!strcmp(out,"survives"));
    CHECK(aw_eval(v,"el.innerHTML='<strong>Bold &amp; valid</strong>';el.textContent",out,sizeof(out))&&!strcmp(out,"Bold & valid"));
    CHECK(aw_eval(v,"el.classList.add('gone');el.hidden=true",out,sizeof(out)));n=aw_export(v,nodes,512);CHECK(!by_id(n,"created"));
    CHECK(aw_eval(v,"globalThis.sequence=[];Promise.resolve().then(()=>sequence.push('promise'));setTimeout(()=>sequence.push('timer'),5);",out,sizeof(out)));aw_poll(v);time_ms=10;aw_poll(v);
    CHECK(aw_eval(v,"sequence.join(',')",out,sizeof(out))&&!strcmp(out,"promise,timer"));
    CHECK(aw_eval(v,"let timer=setInterval(()=>sequence.push('interval'),2);clearInterval(timer);",out,sizeof(out)));time_ms=20;aw_poll(v);CHECK(aw_eval(v,"sequence.length",out,sizeof(out))&&!strcmp(out,"2"));
    CHECK(aw_eval(v,"JSON.stringify([Math.sin(Math.PI/2),2n**64n===18446744073709551616n,/Türkçe/u.test('Türkçe')])",out,sizeof(out))&&!strcmp(out,"[1,true,true]"));
    CHECK(!aw_eval(v,"document.body.appendChild(document.body)",out,sizeof(out)));
    CHECK(!aw_eval(v,"document.querySelector('[')",out,sizeof(out)));
    CHECK(!aw_eval(v,"while(true){}",out,sizeof(out)));CHECK(aw_eval(v,"21*2",out,sizeof(out))&&!strcmp(out,"42"));
    CHECK(aw_load(second,"/ui/second.html","<p>Second document</p>",22,1));CHECK(aw_eval(second,"typeof loaded",out,sizeof(out))&&!strcmp(out,"undefined"));
    for(int i=0;i<30;i++){CHECK(aw_load(v,"/ui/index.html",html,strlen(html),1));CHECK(aw_export(v,nodes,512)>0);}
    aw_destroy(second);aw_destroy(v);
    puts("PASS: arena reclamation, paths, real HTML/CSS cascade, external JS/modules, DOM mutation, clicks, cancellation, promises, timers, JS limits, document isolation and reload.");return 0;
}
