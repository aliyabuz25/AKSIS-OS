/*
 * SHENLONG OS - Bellek Yöneticisi (64-bit)
 * Limine memory map'ten en büyük free bölgeyi heap olarak kullan.
 * Statik array yok — BSS küçük kalır.
 */

#include "kernel.h"
#include "limine.h"

/* memmap kernel_info.c'de tanımlı, hhdm kernel.c'de */
extern volatile struct limine_memmap_request memmap_req;
extern volatile struct limine_hhdm_request   hhdm_request;
/* Alias — kod geri kalanı memmap_request ismiyle erişiyor */
#define memmap_request memmap_req

static uint8_t *heap_base = NULL;
static size_t   heap_size = 0;
static size_t   heap_ptr  = 0;

#define HEAP_MIN  (32ULL * 1024 * 1024)        /* En az 32MB iste */
#define HEAP_MAX  (6ULL * 1024 * 1024 * 1024)  /* 6GB dinamik heap desteği */

void mem_init(void) {
    heap_ptr = 0;

    if (!memmap_request.response || !hhdm_request.response) {
        kernel_panic("mem_init: Limine memory map alinamadi");
    }

    uint64_t hhdm = hhdm_request.response->offset;
    struct limine_memmap_response *mm = memmap_request.response;

    /* En büyük USABLE bölgeyi bul */
    uint64_t best_base = 0, best_len = 0;
    for (uint64_t i = 0; i < mm->entry_count; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        /* 1MB altını atla (DMA bölgesi) */
        if (e->base < 0x100000) continue;
        if (e->length > best_len) {
            best_len  = e->length;
            best_base = e->base;
        }
    }

    if (best_len < HEAP_MIN)
        kernel_panic("mem_init: Yeterli fiziksel bellek yok");

    /* İstenen kadarını al (6GB sınırına kadar donanımın izin verdiği tam alan) */
    if (best_len > HEAP_MAX) best_len = HEAP_MAX;

    heap_base = (uint8_t *)(best_base + hhdm);
    heap_size = (size_t)best_len;

    /* İlk 4MB'ı başlangıçta sıfırla; kalan alan kmalloc çağrılarında anlık sıfırlanır */
    size_t init_clear = heap_size < (4ULL * 1024 * 1024) ? heap_size : (4ULL * 1024 * 1024);
    kmemset(heap_base, 0, init_clear);
}

static inline void dbg_putc(char c) { __asm__ volatile("outb %0, %1" : : "a"(c), "Nd"((uint16_t)0x3f8)); }
static void dbg_puts(const char *s) { while (*s) dbg_putc(*s++); }

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    if (!heap_base) kernel_panic("kmalloc: heap hazir degil");
    size_t aligned = (size + 15) & ~(size_t)15;
    if (aligned > heap_size - heap_ptr) {
        dbg_puts("\n[KMALLOC PANIC] requested size=");
        char buf[32]; kitoa((int64_t)aligned, buf); dbg_puts(buf);
        dbg_puts(" heap_ptr="); kitoa((int64_t)heap_ptr, buf); dbg_puts(buf);
        dbg_puts(" heap_size="); kitoa((int64_t)heap_size, buf); dbg_puts(buf);
        dbg_puts("\n");
        kernel_panic("kmalloc: heap doldu");
    }
    void *ptr = heap_base + heap_ptr;
    heap_ptr += aligned;
    kmemset(ptr, 0, aligned);
    return ptr;
}

void kfree(void *ptr) { (void)ptr; }

void *kmemset(void *ptr, int val, size_t n) {
    uint8_t *p = (uint8_t *)ptr;
    if (n >= 16) {
        uint64_t v8 = (uint8_t)val;
        v8 |= (v8 << 8);
        v8 |= (v8 << 16);
        v8 |= (v8 << 32);
        while (((uintptr_t)p & 7) && n) {
            *p++ = (uint8_t)val;
            n--;
        }
        uint64_t *p64 = (uint64_t *)p;
        while (n >= 32) {
            p64[0] = v8;
            p64[1] = v8;
            p64[2] = v8;
            p64[3] = v8;
            p64 += 4;
            n -= 32;
        }
        while (n >= 8) {
            *p64++ = v8;
            n -= 8;
        }
        p = (uint8_t *)p64;
    }
    while (n--) *p++ = (uint8_t)val;
    return ptr;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (n >= 16 && (((uintptr_t)d & 7) == ((uintptr_t)s & 7))) {
        while (((uintptr_t)d & 7) && n) {
            *d++ = *s++;
            n--;
        }
        uint64_t *d64 = (uint64_t *)d;
        const uint64_t *s64 = (const uint64_t *)s;
        while (n >= 32) {
            d64[0] = s64[0];
            d64[1] = s64[1];
            d64[2] = s64[2];
            d64[3] = s64[3];
            d64 += 4;
            s64 += 4;
            n -= 32;
        }
        while (n >= 8) {
            *d64++ = *s64++;
            n -= 8;
        }
        d = (uint8_t *)d64;
        s = (const uint8_t *)s64;
    }
    while (n--) *d++ = *s++;
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    while (n--) { if (*p != *q) return (int)*p - (int)*q; p++; q++; }
    return 0;
}

int kstrlen(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, int n) {
    while (n-- && *a && (*a == *b)) { a++; b++; }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *kstrcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *kstrcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char *kstrchr(const char *s, char c) {
    while (*s) {
        if (*s == c) return (char *)s;
        s++;
    }
    return NULL;
}

void kitoa(int64_t n, char *buf) {
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    int neg = (n < 0);
    if (neg) n = -n;
    int i = 0;
    char tmp[21];
    while (n > 0) { tmp[i++] = (char)('0' + (n % 10)); n /= 10; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i--) buf[j++] = tmp[i];
    buf[j] = '\0';
}

int64_t katoi(const char *s) {
    int64_t n = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}
