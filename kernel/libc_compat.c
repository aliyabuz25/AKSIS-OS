#include "kernel.h"
void *memcpy(void *d,const void *s,size_t n) { return kmemcpy(d,s,n); }
void *memset(void *p,int c,size_t n) { return kmemset(p,c,n); }
int memcmp(const void *a,const void *b,size_t n) { return kmemcmp(a,b,n); }
size_t strlen(const char *s) { return kstrlen(s); }
int strcmp(const char *a,const char *b) { return kstrcmp(a,b); }
void *memmove(void *d,const void *s,size_t n) {
    uint8_t *a=d; const uint8_t *b=s;
    if(a>b) { while(n) { n--; a[n]=b[n]; } }
    else { for(size_t i=0;i<n;i++) a[i]=b[i]; }
    return d;
}
int abs(int a) { return a<0?-a:a; }
