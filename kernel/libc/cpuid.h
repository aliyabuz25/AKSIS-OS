#ifndef SHENLONG_CPUID_H
#define SHENLONG_CPUID_H
static inline int __get_cpuid(unsigned leaf,unsigned *a,unsigned *b,unsigned *c,unsigned *d) {
    unsigned max,x,y,z;
    __asm__ volatile("cpuid":"=a"(max),"=b"(x),"=c"(y),"=d"(z):"a"(0),"c"(0));
    if(leaf>max) return 0;
    __asm__ volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(leaf),"c"(0));
    return 1;
}
#endif
