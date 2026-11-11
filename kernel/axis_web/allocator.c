/* Coalescing, 16-byte aligned arena; independent of the kernel bump heap. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct block {
    size_t size;
    struct block *next, *prev;
    size_t used;
} block;
static block *first;
static size_t live, peak;

void axis_memory_init(void *memory, size_t bytes) {
    uintptr_t p = ((uintptr_t)memory + 15) & ~(uintptr_t)15;
    size_t skip = p - (uintptr_t)memory;
    first = NULL; live = peak = 0;
    if (!memory || bytes <= skip + sizeof(block)) return;
    first = (block *)p;
    *first = (block){.size = (bytes - skip - sizeof(block)) & ~(size_t)15};
}
size_t axis_memory_live(void) { return live; }
size_t axis_memory_peak(void) { return peak; }
size_t axis_malloc_size(const void *p) { return p ? ((const block *)p)[-1].size : 0; }

static void split(block *b, size_t n) {
    if (b->size < n + sizeof(block) + 16) return;
    block *rest = (block *)((char *)(b + 1) + n);
    *rest = (block){.size = b->size - n - sizeof(block), .next = b->next, .prev = b};
    if (rest->next) rest->next->prev = rest;
    b->next = rest; b->size = n;
}
void *axis_malloc(size_t n) {
    if (!n || n > SIZE_MAX - 15) return NULL;
    n = (n + 15) & ~(size_t)15;
    for (block *b = first; b; b = b->next) if (!b->used && b->size >= n) {
        split(b, n); b->used = 1; live += b->size;
        if (live > peak) peak = live;
        return b + 1;
    }
    return NULL;
}
static void merge(block *b) {
    block *next = b->next;
    if (next && !next->used) {
        b->size += sizeof(block) + next->size; b->next = next->next;
        if (b->next) b->next->prev = b;
    }
}
void axis_free(void *p) {
    if (!p) return;
    block *b = (block *)p - 1;
    live -= b->size; b->used = 0;
    merge(b);
    if (b->prev && !b->prev->used) merge(b->prev);
}
void *axis_calloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return NULL;
    void *p = axis_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
void *axis_realloc(void *p, size_t n) {
    if (!p) return axis_malloc(n);
    if (!n) { axis_free(p); return NULL; }
    if (n > SIZE_MAX - 15) return NULL;
    block *b = (block *)p - 1;
    size_t old = b->size, aligned = (n + 15) & ~(size_t)15;
    if (aligned > b->size && b->next && !b->next->used &&
        b->size + sizeof(block) + b->next->size >= aligned) merge(b);
    if (aligned <= b->size) {
        split(b, aligned);
        if (b->next && !b->next->used) merge(b->next);
        live = live - old + b->size;
        if (live > peak) peak = live;
        return p;
    }
    void *q = axis_malloc(n);
    if (!q) return NULL;
    memcpy(q, p, old); axis_free(p); return q;
}
