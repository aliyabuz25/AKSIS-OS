#ifndef SHENLONG_ATA_H
#define SHENLONG_ATA_H

#include "kernel.h"

typedef struct {
    int      present;
    uint16_t base;       /* 0x1F0 (Primary) veya 0x170 (Secondary) */
    uint8_t  drive;      /* 0 = Master, 1 = Slave */
    uint32_t sectors;    /* 28-bit LBA toplam sektör sayısı */
    char     model[41];  /* ASCII model adı */
} ata_drive_t;

extern ata_drive_t g_ata_drive;

int         ata_init(void);
int         ata_drive_present(void);
uint32_t    ata_sector_count(void);
const char *ata_model_name(void);
int         ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int         ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);

#endif /* SHENLONG_ATA_H */
