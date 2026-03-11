#ifndef IDE_H
#define IDE_H

#include <stdint.h>

int ide_read_sector(uint32_t lba, uint8_t *buffer);
int ide_write_sector(uint32_t lba, uint8_t *buffer);

#endif
