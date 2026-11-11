/*
 * SHENLONG OS - PAT Write-Combining
 * PAT[7] = WC, framebuffer sayfalarini WC olarak remap et
 */

#include "pat.h"
#include "kernel.h"

/* ------------------------------------------------------------------ */
/* MSR okuma/yazma                                                     */
/* ------------------------------------------------------------------ */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}
static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" ::
        "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

/* ------------------------------------------------------------------ */
/* PAT MSR ayarla: PAT[7] = WC                                        */
/* ------------------------------------------------------------------ */
void pat_init(void) {
    /* IA32_PAT MSR = 0x277
     * Varsayilan: PA0=WB,PA1=WT,PA2=UC-,PA3=UC,PA4=WB,PA5=WT,PA6=UC-,PA7=UC
     * Hedef:      PA7 = WC (0x01) */
    uint64_t pat = rdmsr(0x277);
    pat &= ~(0xFFULL << 56);        /* PA7 temizle */
    pat |=  ((uint64_t)PAT_WC << 56); /* PA7 = WC */
    wrmsr(0x277, pat);
}

/* ------------------------------------------------------------------ */
/* Sayfa tablosu walk + WC flag set                                   */
/* PTE flags for PAT index 7: PAT=1(bit7), PCD=1(bit4), PWT=1(bit3) */
/* ------------------------------------------------------------------ */
#define PTE_PAT_BIT  (1ULL << 7)
#define PTE_PCD_BIT  (1ULL << 4)
#define PTE_PWT_BIT  (1ULL << 3)
#define PTE_PRESENT  (1ULL << 0)

/* CR3'ten sayfa tablosunu al */
static inline uint64_t get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
static inline void flush_tlb(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

/* Verilen virtual adres icin PTE pointer'ini bul (4KB sayfalar) */
static uint64_t *find_pte(uint64_t virt) {
    /* Limine kernel'de higher half: virtual adres direkt erisilebilir */
    uint64_t cr3   = get_cr3() & ~0xFFFULL;
    uint64_t pml4i = (virt >> 39) & 0x1FF;
    uint64_t pdpti = (virt >> 30) & 0x1FF;
    uint64_t pdi   = (virt >> 21) & 0x1FF;
    uint64_t pti   = (virt >> 12) & 0x1FF;

    uint64_t *pml4 = (uint64_t *)cr3;
    if (!(pml4[pml4i] & PTE_PRESENT)) return NULL;

    uint64_t *pdpt = (uint64_t *)(pml4[pml4i] & ~0xFFFULL);
    if (!(pdpt[pdpti] & PTE_PRESENT)) return NULL;
    /* 1GB buyuk sayfa kontrolu */
    if (pdpt[pdpti] & (1ULL << 7)) return NULL;

    uint64_t *pd = (uint64_t *)(pdpt[pdpti] & ~0xFFFULL);
    if (!(pd[pdi] & PTE_PRESENT)) return NULL;
    /* 2MB buyuk sayfa kontrolu */
    if (pd[pdi] & (1ULL << 7)) return NULL;

    uint64_t *pt = (uint64_t *)(pd[pdi] & ~0xFFFULL);
    if (!(pt[pti] & PTE_PRESENT)) return NULL;

    return &pt[pti];
}

/* ------------------------------------------------------------------ */
/* Framebuffer sayfalarini WC olarak remap et                         */
/* ------------------------------------------------------------------ */
void pat_remap_fb_wc(uint64_t virt, uint64_t phys, uint64_t size) {
    (void)phys; /* zaten mapped, sadece flag degistirecegiz */

    /* 4KB sayfa sinirina hizala */
    uint64_t page  = virt & ~0xFFFULL;
    uint64_t end   = (virt + size + 0xFFF) & ~0xFFFULL;

    for (; page < end; page += 0x1000) {
        uint64_t *pte = find_pte(page);
        if (!pte) continue;

        /* Cache flag bitlerini temizle */
        *pte &= ~(PTE_PAT_BIT | PTE_PCD_BIT | PTE_PWT_BIT);
        /* PAT index 7 = PAT+PCD+PWT */
        *pte |= (PTE_PAT_BIT | PTE_PCD_BIT | PTE_PWT_BIT);

        flush_tlb(page);
    }

    /* TLB full flush */
    __asm__ volatile(
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}
