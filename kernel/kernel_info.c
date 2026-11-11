/*
 * SHENLONG OS - Sistem Bilgisi Toplama
 * Limine'den gerçek donanım verileri
 */

#include "kernel_info.h"
#include "kernel.h"
#include "limine.h"

sysinfo_t g_sysinfo;

/* ------------------------------------------------------------------ */
/* Limine istekleri (kernel.c'deki requests section'a eklenecek)     */
/* Bu dosyadan extern olarak erişilir                                 */
/* ------------------------------------------------------------------ */

/* Bellek haritası */
__attribute__((used, section(".requests")))
volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0, .response = NULL
};

/* Firmware tipi */
__attribute__((used, section(".requests")))
volatile struct limine_firmware_type_request fw_req = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID,
    .revision = 0, .response = NULL
};

/* TSC frekansı (gerçek CPU MHz) */
__attribute__((used, section(".requests")))
volatile struct limine_tsc_frequency_request tsc_req = {
    .id = LIMINE_TSC_FREQUENCY_REQUEST_ID,
    .revision = 0, .response = NULL
};

/* Boot tarihi/saati */
__attribute__((used, section(".requests")))
volatile struct limine_date_at_boot_request date_req = {
    .id = LIMINE_DATE_AT_BOOT_REQUEST_ID,
    .revision = 0, .response = NULL
};

/* CPU sayısı (MP) */
__attribute__((used, section(".requests")))
volatile struct limine_mp_request mp_req = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0, .response = NULL,
    .flags = 0
};

/* Kernel adresi */
__attribute__((used, section(".requests")))
volatile struct limine_executable_address_request kaddr_req = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0, .response = NULL
};

/* Paging modu */
__attribute__((used, section(".requests")))
volatile struct limine_paging_mode_request paging_req = {
    .id = LIMINE_PAGING_MODE_REQUEST_ID,
    .revision = 0, .response = NULL,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_5LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

/* ------------------------------------------------------------------ */
/* rdtsc                                                              */
/* ------------------------------------------------------------------ */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* ------------------------------------------------------------------ */
/* CPUID ile marka string'i al                                        */
/* ------------------------------------------------------------------ */
static char cpu_brand[49];

static void get_cpu_brand(void) {
    uint32_t regs[12];
    /* EAX=0x80000002,3,4 */
    for (int i = 0; i < 3; i++) {
        uint32_t eax = 0x80000002 + (uint32_t)i;
        __asm__ volatile(
            "cpuid"
            : "=a"(regs[i*4+0]), "=b"(regs[i*4+1]),
              "=c"(regs[i*4+2]), "=d"(regs[i*4+3])
            : "a"(eax)
        );
    }
    kmemcpy(cpu_brand, regs, 48);
    cpu_brand[48] = '\0';
    /* Baştaki boşlukları temizle */
    char *p = cpu_brand;
    while (*p == ' ') p++;
    if (p != cpu_brand) {
        int i = 0;
        while ((cpu_brand[i] = p[i])) i++;
    }
}

/* ------------------------------------------------------------------ */
/* Yardımcı fonksiyonlar                                              */
/* ------------------------------------------------------------------ */
void fmt_size(uint64_t bytes, char *out) {
    char num[32];
    if (bytes >= 1024ULL*1024*1024) {
        kitoa((int64_t)(bytes / (1024*1024*1024)), num);
        kstrcpy(out, num); kstrcat(out, " GB");
    } else if (bytes >= 1024*1024) {
        kitoa((int64_t)(bytes / (1024*1024)), num);
        kstrcpy(out, num); kstrcat(out, " MB");
    } else if (bytes >= 1024) {
        kitoa((int64_t)(bytes / 1024), num);
        kstrcpy(out, num); kstrcat(out, " KB");
    } else {
        kitoa((int64_t)bytes, num);
        kstrcpy(out, num); kstrcat(out, " B");
    }
}

void fmt_freq(uint64_t hz, char *out) {
    char num[32];
    if (hz >= 1000000000ULL) {
        uint64_t ghz  = hz / 1000000000ULL;
        uint64_t mhz  = (hz % 1000000000ULL) / 100000000ULL;
        kitoa((int64_t)ghz, num);
        kstrcpy(out, num); kstrcat(out, ".");
        kitoa((int64_t)mhz, num);
        kstrcat(out, num); kstrcat(out, " GHz");
    } else {
        kitoa((int64_t)(hz / 1000000ULL), num);
        kstrcpy(out, num); kstrcat(out, " MHz");
    }
}

uint64_t get_uptime_ms(void) {
    if (g_sysinfo.tsc_freq_hz < 1000) return 0;
    uint64_t now  = rdtsc();
    uint64_t diff = now - g_sysinfo.boot_tsc;
    return diff / (g_sysinfo.tsc_freq_hz / 1000);
}

const char *get_cpu_brand_str(void) { return cpu_brand; }

/* ------------------------------------------------------------------ */
/* Ana init                                                           */
/* ------------------------------------------------------------------ */
void sysinfo_init(void) {
    kmemset(&g_sysinfo, 0, sizeof(sysinfo_t));

    /* Boot TSC */
    g_sysinfo.boot_tsc = rdtsc();

    /* CPU brand */
    get_cpu_brand();

    /* Bellek haritası */
    if (memmap_req.response) {
        uint64_t total = 0, usable = 0;
        for (uint64_t i = 0; i < memmap_req.response->entry_count; i++) {
            struct limine_memmap_entry *e = memmap_req.response->entries[i];
            /* Reserved MMIO/PCI apertures and framebuffer space are not RAM. */
            if (e->type == LIMINE_MEMMAP_USABLE ||
                e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES ||
                e->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE ||
                e->type == LIMINE_MEMMAP_ACPI_NVS)
                total += e->length;
            if (e->type == LIMINE_MEMMAP_USABLE ||
                e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
                e->type == LIMINE_MEMMAP_ACPI_RECLAIMABLE) {
                usable += e->length;
            }
        }
        g_sysinfo.total_memory  = total;
        g_sysinfo.usable_memory = usable;
        g_sysinfo.memmap_entries = (uint32_t)memmap_req.response->entry_count;
    }

    /* Firmware tipi */
    if (fw_req.response)
        g_sysinfo.firmware_type = (uint32_t)fw_req.response->firmware_type;

    /* TSC frekansı */
    if (tsc_req.response)
        g_sysinfo.tsc_freq_hz = tsc_req.response->frequency;

    /* Boot tarihi - Unix timestamp'i parçala */
    if (date_req.response) {
        int64_t ts = date_req.response->timestamp;
        g_sysinfo.boot_time = ts;

        /* ts -> yıl/ay/gün/saat/dk/sn */
        /* Basit Gregorian hesaplama */
        int64_t days    = ts / 86400;
        int64_t rem     = ts % 86400;
        g_sysinfo.boot_hour   = (uint8_t)(rem / 3600);
        g_sysinfo.boot_minute = (uint8_t)((rem % 3600) / 60);
        g_sysinfo.boot_second = (uint8_t)(rem % 60);

        /* 1970'ten gün sayısını yıl/ay/güne çevir */
        int64_t z = days + 719468;
        int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        int64_t doe = z - era * 146097;
        int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        int64_t y   = yoe + era * 400;
        int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
        int64_t mp  = (5*doy + 2) / 153;
        int64_t d   = doy - (153*mp + 2)/5 + 1;
        int64_t m   = mp < 10 ? mp+3 : mp-9;
        y += (m <= 2);

        g_sysinfo.boot_year  = (uint16_t)y;
        g_sysinfo.boot_month = (uint8_t)m;
        g_sysinfo.boot_day   = (uint8_t)d;
    }

    /* CPU sayısı */
    if (mp_req.response)
        g_sysinfo.cpu_count = (uint32_t)mp_req.response->cpu_count;
    else
        g_sysinfo.cpu_count = 1;

    /* Kernel adresi */
    if (kaddr_req.response) {
        g_sysinfo.kernel_phys_base = kaddr_req.response->physical_base;
        g_sysinfo.kernel_virt_base = kaddr_req.response->virtual_base;
    }

    /* Paging modu */
    if (paging_req.response)
        g_sysinfo.paging_mode = (uint32_t)paging_req.response->mode;
}
