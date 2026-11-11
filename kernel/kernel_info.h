/*
 * SHENLONG OS - Kernel Sistem Bilgileri
 * Limine'den alınan gerçek donanım verileri
 */

#ifndef KERNEL_INFO_H
#define KERNEL_INFO_H

#include "kernel.h"

/* ------------------------------------------------------------------ */
/* Merkezi sistem bilgisi yapısı                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    /* Bootloader */
    const char  *bootloader_name;
    const char  *bootloader_version;

    /* Firmware */
    uint32_t     firmware_type;     /* 0=BIOS, 1=EFI32, 2=EFI64 */

    /* Bellek */
    uint64_t     total_memory;      /* byte cinsinden toplam RAM */
    uint64_t     usable_memory;     /* kullanılabilir RAM */
    uint32_t     memmap_entries;

    /* CPU */
    uint64_t     tsc_freq_hz;       /* TSC frekansı (Hz) */
    uint32_t     cpu_count;         /* mantıksal CPU sayısı */

    /* Tarih/saat (boot anı) */
    int64_t      boot_time;         /* Unix timestamp */
    uint16_t     boot_year;
    uint8_t      boot_month;
    uint8_t      boot_day;
    uint8_t      boot_hour;
    uint8_t      boot_minute;
    uint8_t      boot_second;

    /* Kernel */
    uint64_t     kernel_phys_base;
    uint64_t     kernel_virt_base;

    /* Framebuffer */
    uint32_t     fb_width;
    uint32_t     fb_height;
    uint32_t     fb_bpp;

    /* Paging */
    uint32_t     paging_mode;       /* 0=4LVL, 1=5LVL */

    /* Boot ticks (rdtsc) */
    uint64_t     boot_tsc;

} sysinfo_t;

extern sysinfo_t g_sysinfo;

void sysinfo_init(void);
const char *get_cpu_brand_str(void);

/* Yardımcı: sayıyı KB/MB/GB formatına çevir */
void fmt_size(uint64_t bytes, char *out);

/* Yardımcı: frekansı MHz/GHz formatına çevir */
void fmt_freq(uint64_t hz, char *out);

/* Yardımcı: uptime hesapla (rdtsc tabanlı) */
uint64_t get_uptime_ms(void);

#endif