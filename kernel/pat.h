/*
 * SHENLONG OS - PAT (Page Attribute Table) Write-Combining
 * Framebuffer yazma hızını 10-40x artırır
 */
#ifndef PAT_H
#define PAT_H

#include "kernel.h"

/* PAT memory tipleri */
#define PAT_WB   0x06  /* Write-Back (normal RAM) */
#define PAT_WC   0x01  /* Write-Combining (FB için) */
#define PAT_UC   0x00  /* Uncacheable */

/* PAT[7] = WC olarak ayarla, FB sayfalarını remap et */
void pat_init(void);

/* Framebuffer virtual adresini WC olarak remap et */
void pat_remap_fb_wc(uint64_t virt, uint64_t phys, uint64_t size);

#endif