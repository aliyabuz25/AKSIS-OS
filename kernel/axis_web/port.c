/* AXIS-only services required by the embedded C libraries. */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#define STB_SPRINTF_IMPLEMENTATION
#include "../../vendor/axis-web/stb/deprecated/stb_sprintf.h"

extern void *axis_malloc(size_t);
extern void *axis_realloc(void *, size_t);
extern void *axis_calloc(size_t, size_t);
extern void axis_free(void *);
extern void kernel_panic(const char *);
extern unsigned long long get_uptime_ms(void);
extern unsigned long long axis_wall_time_ms(void);
void *malloc(size_t n) { return axis_malloc(n); }
void *calloc(size_t n, size_t s) { return axis_calloc(n,s); }
void *realloc(void *p, size_t n) { return axis_realloc(p,n); }
void free(void *p) { axis_free(p); }
static int axis_errno;
int *__errno_location(void) { return &axis_errno; }
unsigned long strtoul(const char *s,char **end,int base) {
    const char *start=s;while(*s==' '||*s=='\t'||*s=='\n'||*s=='\r'||*s=='\f'||*s=='\v')s++;
    int neg=0;if(*s=='+'||*s=='-')neg=*s++=='-';
    if(base&&(base<2||base>36)){axis_errno=22;if(end)*end=(char *)start;return 0;}
    if((base==0||base==16)&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')&&((s[2]>='0'&&s[2]<='9')||(s[2]>='a'&&s[2]<='f')||(s[2]>='A'&&s[2]<='F'))){base=16;s+=2;}
    if(!base)base=*s=='0'?8:10;
    unsigned long value=0;int any=0,overflow=0;
    for(;;s++){unsigned d=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='z'?*s-'a'+10:*s>='A'&&*s<='Z'?*s-'A'+10:99;if(d>=(unsigned)base)break;any=1;if(value>(ULONG_MAX-d)/(unsigned)base)overflow=1;else value=value*base+d;}
    if(end)*end=(char *)(any?s:start);if(overflow){axis_errno=34;return ULONG_MAX;}return neg?0-value:value;
}
_Noreturn void abort(void) { kernel_panic("AXIS web engine assertion"); for (;;) {} }
FILE *const stdout = (FILE *)1;
FILE *const stderr = (FILE *)2;
int fputc(int c, FILE *f) {
    (void)f;
    __asm__ volatile("outb %0, %1" :: "a"((unsigned char)c), "Nd"((unsigned short)0x3f8));
    return (unsigned char)c;
}
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char *s, FILE *f) { int n=0; while (*s) { fputc(*s++,f); n++; } return n; }
int puts(const char *s) { int n=fputs(s,stdout); fputc('\n',stdout); return n+1; }
size_t fwrite(const void *p, size_t size, size_t count, FILE *f) {
    if (size && count > SIZE_MAX / size) return 0;
    const unsigned char *s=p; for (size_t i=0; i<size*count; i++) fputc(s[i],f);
    return count;
}
int fflush(FILE *f) { (void)f; return 0; }
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap) {
    return stbsp_vsnprintf(s, n>INT_MAX?INT_MAX:(int)n, fmt, ap);
}
int snprintf(char *s, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); int r=vsnprintf(s,n,fmt,ap); va_end(ap); return r;
}
int sprintf(char *s, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); int r=stbsp_vsprintf(s,fmt,ap); va_end(ap); return r;
}
int fprintf(FILE *f, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap,fmt);
    int r=vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap); fputs(buf,f); return r;
}
int printf(const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap,fmt);
    int r=vsnprintf(buf,sizeof(buf),fmt,ap); va_end(ap); fputs(buf,stdout); return r;
}
int gettimeofday(struct timeval *tv, void *zone) {
    (void)zone; unsigned long long ms=axis_wall_time_ms();
    tv->tv_sec=ms/1000; tv->tv_usec=(ms%1000)*1000; return 0;
}
extern int __secs_to_tm(long long, struct tm *);
struct tm *localtime_r(const time_t *t, struct tm *out) {
    if (__secs_to_tm(*t,out)) return NULL;
    out->tm_gmtoff=0; out->tm_zone="UTC"; out->tm_isdst=0; return out;
}
