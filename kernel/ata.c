/*
 * SHENLONG OS - ATA/IDE Sürücüsü (PIO 28-bit LBA)
 * QEMU, UTM ve gerçek donanımda kalıcı disk erişimi
 */

#include "ata.h"
#include "kernel.h"

ata_drive_t g_ata_drive;

static inline void ata_delay(uint16_t base) {
    inb(base + 7);
    inb(base + 7);
    inb(base + 7);
    inb(base + 7);
}

static int ata_wait_ready(uint16_t base) {
    int timeout = 100000;
    while ((inb(base + 7) & 0x80) && --timeout) {
        __asm__ volatile("pause");
    }
    return timeout > 0;
}

static int ata_wait_drq(uint16_t base) {
    int timeout = 100000;
    while (--timeout) {
        uint8_t st = inb(base + 7);
        if (st & 0x01) return 0; /* ERR */
        if (st & 0x08) return 1; /* DRQ */
        __asm__ volatile("pause");
    }
    return 0;
}

static int ata_identify(uint16_t base, uint8_t drive, ata_drive_t *dev) {
    /* Sürücü seçimi */
    outb(base + 6, 0xA0 | (drive << 4));
    ata_delay(base);

    uint8_t st = inb(base + 7);
    if (st == 0xFF || st == 0x00) return 0; /* Veri yolu boş */

    outb(base + 2, 0);
    outb(base + 3, 0);
    outb(base + 4, 0);
    outb(base + 5, 0);
    outb(base + 7, 0xEC); /* IDENTIFY komutu */
    ata_delay(base);

    st = inb(base + 7);
    if (st == 0) return 0;

    int timeout = 100000;
    while ((inb(base + 7) & 0x80) && --timeout) {
        __asm__ volatile("pause");
    }
    if (!timeout) return 0;

    uint8_t cl = inb(base + 4);
    uint8_t ch = inb(base + 5);
    /* ATAPI (CD-ROM / Optik sürücü) ise sabit disk değildir, atla */
    if ((cl == 0x14 && ch == 0xEB) || (cl == 0x69 && ch == 0x96)) {
        return 0;
    }

    /* DRQ bekle */
    if (!ata_wait_drq(base)) return 0;

    /* 256 kelime (512 bayt) oku */
    uint16_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = inw(base);
    }

    dev->present = 1;
    dev->base    = base;
    dev->drive   = drive;

    /* 28-bit LBA sektör sayısı: kelime 60 ve 61 */
    dev->sectors = (uint32_t)data[60] | ((uint32_t)data[61] << 16);
    if (dev->sectors == 0) {
        uint32_t c = data[1], h = data[3], s = data[6];
        dev->sectors = c * h * s;
    }

    /* Model adı: kelime 27-46 (bayt sıralı) */
    int idx = 0;
    for (int i = 27; i <= 46; i++) {
        dev->model[idx++] = (char)(data[i] >> 8);
        dev->model[idx++] = (char)(data[i] & 0xFF);
    }
    dev->model[40] = '\0';
    for (int i = 39; i >= 0 && (dev->model[i] == ' ' || dev->model[i] == '\0'); i--) {
        dev->model[i] = '\0';
    }
    if (dev->model[0] == '\0') {
        kstrcpy(dev->model, "SHENLONG ATA DISK");
    }

    return 1;
}

int ata_init(void) {
    kmemset(&g_ata_drive, 0, sizeof(g_ata_drive));

    /* Olası ATA kanallarını ve sürücülerini tara */
    static const struct { uint16_t base; uint8_t drive; } probes[] = {
        { 0x1F0, 1 }, /* Primary Slave (UTM / QEMU CD-ROM Master iken) */
        { 0x170, 0 }, /* Secondary Master */
        { 0x170, 1 }, /* Secondary Slave */
        { 0x1F0, 0 }, /* Primary Master */
    };

    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        if (ata_identify(probes[i].base, probes[i].drive, &g_ata_drive)) {
            return 1;
        }
    }
    return 0;
}

int ata_drive_present(void) {
    return g_ata_drive.present;
}

uint32_t ata_sector_count(void) {
    return g_ata_drive.sectors;
}

const char *ata_model_name(void) {
    return g_ata_drive.present ? g_ata_drive.model : "Yok";
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!g_ata_drive.present || !count || !buf) return 0;
    uint16_t base  = g_ata_drive.base;
    uint8_t  drive = g_ata_drive.drive;
    uint16_t *dst  = (uint16_t *)buf;

    for (uint8_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        if (!ata_wait_ready(base)) return 0;

        outb(base + 6, 0xE0 | (drive << 4) | ((cur >> 24) & 0x0F));
        ata_delay(base);

        outb(base + 2, 1);
        outb(base + 3, (uint8_t)cur);
        outb(base + 4, (uint8_t)(cur >> 8));
        outb(base + 5, (uint8_t)(cur >> 16));
        outb(base + 7, 0x20); /* READ SECTORS */

        if (!ata_wait_drq(base)) return 0;

        for (int i = 0; i < 256; i++) {
            *dst++ = inw(base);
        }
    }
    return 1;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!g_ata_drive.present || !count || !buf) return 0;
    uint16_t base       = g_ata_drive.base;
    uint8_t  drive      = g_ata_drive.drive;
    const uint16_t *src = (const uint16_t *)buf;

    for (uint8_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        if (!ata_wait_ready(base)) return 0;

        outb(base + 6, 0xE0 | (drive << 4) | ((cur >> 24) & 0x0F));
        ata_delay(base);

        outb(base + 2, 1);
        outb(base + 3, (uint8_t)cur);
        outb(base + 4, (uint8_t)(cur >> 8));
        outb(base + 5, (uint8_t)(cur >> 16));
        outb(base + 7, 0x30); /* WRITE SECTORS */

        if (!ata_wait_drq(base)) return 0;

        for (int i = 0; i < 256; i++) {
            outw(base, *src++);
        }

        /* FLUSH CACHE (0xE7) */
        outb(base + 7, 0xE7);
        ata_wait_ready(base);
    }
    return 1;
}
