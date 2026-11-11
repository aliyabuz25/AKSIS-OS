/*
 * SHENLONG OS - Kernel Header (64-bit)
 */

#ifndef KERNEL_H
#define KERNEL_H

/* Temel tipler - 64-bit */
typedef unsigned char           uint8_t;
typedef unsigned short          uint16_t;
typedef unsigned int            uint32_t;
typedef unsigned long long      uint64_t;
typedef signed char             int8_t;
typedef signed short            int16_t;
typedef signed int              int32_t;
typedef signed long long        int64_t;
typedef unsigned long long      size_t;
typedef long long               ssize_t;
typedef unsigned long long      uintptr_t;

#define NULL    ((void*)0)
#define TRUE    1
#define FALSE   0

/* Hizalama */
#define ALIGN(x, a)     (((x) + (a) - 1) & ~((a) - 1))

/* Kernel panik */
void kernel_panic(const char *msg);

/* IDT */
void idt_init(void);

/* Bellek */
void mem_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kmemset(void *ptr, int val, size_t n);
void *kmemcpy(void *dst, const void *src, size_t n);
int   kmemcmp(const void *a, const void *b, size_t n);

/* String yardımcıları */
int     kstrlen(const char *s);
int     kstrcmp(const char *a, const char *b);
int     kstrncmp(const char *a, const char *b, int n);
char   *kstrcpy(char *dst, const char *src);
char   *kstrcat(char *dst, const char *src);
char   *kstrchr(const char *s, char c);
void    kitoa(int64_t n, char *buf);
int64_t katoi(const char *s);

/* I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#endif /* KERNEL_H */
