#include "ide.h"
#include "../../cpu/ports.h"
#include "../../core/kernel_api.h"

#define IDE_DATA        0x1F0
#define IDE_ERROR       0x1F1
#define IDE_SECTOR_CNT  0x1F2
#define IDE_LBA_LOW     0x1F3
#define IDE_LBA_MID     0x1F4
#define IDE_LBA_HIGH    0x1F5
#define IDE_DRIVE_HEAD  0x1F6
#define IDE_STATUS      0x1F7
#define IDE_COMMAND     0x1F7

#define IDE_STATUS_BSY  0x80
#define IDE_STATUS_RDY  0x40
#define IDE_STATUS_DRQ  0x08
#define IDE_STATUS_ERR  0x01

#define IDE_CMD_READ    0x20
#define IDE_CMD_WRITE   0x30
#define IDE_CMD_CACHE_FLUSH 0xE7

static void ide_wait_bsy() {
    while (port_byte_in(IDE_STATUS) & IDE_STATUS_BSY);
}

static int ide_wait_drq() {
    while (1) {
        uint8_t s = port_byte_in(IDE_STATUS);
        if (s & IDE_STATUS_ERR) return -1;
        if (s & IDE_STATUS_DRQ) return 0;
    }
}

static void ide_delay_400ns() {
    port_byte_in(IDE_STATUS);
    port_byte_in(IDE_STATUS);
    port_byte_in(IDE_STATUS);
    port_byte_in(IDE_STATUS);
}

int ide_read_sector(uint32_t lba, uint8_t *buffer) {
    ide_wait_bsy();
    
    port_byte_out(IDE_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ide_delay_400ns();
    
    port_byte_out(IDE_SECTOR_CNT, 1);
    port_byte_out(IDE_LBA_LOW, (uint8_t)lba);
    port_byte_out(IDE_LBA_MID, (uint8_t)(lba >> 8));
    port_byte_out(IDE_LBA_HIGH, (uint8_t)(lba >> 16));
    port_byte_out(IDE_COMMAND, IDE_CMD_READ);

    ide_wait_bsy();

    uint8_t status = port_byte_in(IDE_STATUS);
    if (status & IDE_STATUS_ERR) {
        kprint("[IDE] read error\n");
        return -1;
    }

    if (ide_wait_drq() != 0) {
        kprint("[IDE] DRQ error during read\n");
        return -1;
    }

    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = port_word_in(IDE_DATA);
    }
    return 0;
}

int ide_write_sector(uint32_t lba, uint8_t *buffer) {
    ide_wait_bsy();

    port_byte_out(IDE_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ide_delay_400ns();

    port_byte_out(IDE_SECTOR_CNT, 1);
    port_byte_out(IDE_LBA_LOW, (uint8_t)lba);
    port_byte_out(IDE_LBA_MID, (uint8_t)(lba >> 8));
    port_byte_out(IDE_LBA_HIGH, (uint8_t)(lba >> 16));
    port_byte_out(IDE_COMMAND, IDE_CMD_WRITE);

    ide_wait_bsy();

    uint8_t status = port_byte_in(IDE_STATUS);
    if (status & IDE_STATUS_ERR) {
        kprint("[IDE] write error\n");
        return -1;
    }

    if (ide_wait_drq() != 0) {
        kprint("[IDE] DRQ error during write\n");
        return -1;
    }

    uint16_t *ptr = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        port_word_out(IDE_DATA, ptr[i]);
    }

    port_byte_out(IDE_COMMAND, IDE_CMD_CACHE_FLUSH);
    ide_wait_bsy();
    return 0;
}
