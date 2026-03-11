#ifndef BLOCK_DEV_H
#define BLOCK_DEV_H

#include <stdint.h>
#include <stddef.h>

typedef int (*block_read_type_t)(uint64_t lba, uint8_t *buf);
typedef int (*block_write_type_t)(uint64_t lba, uint8_t *buf);

#include "../modules/partition/mbr.h"

typedef struct {
    char name[32];
    block_read_type_t read_sector;
    block_write_type_t write_sector;
    uint64_t size; // in sectors
    int is_partition;
    uint32_t parent_dev;
    uint64_t start_lba;
} kabi_block_device_t;

void block_dev_init();
int block_dev_register(kabi_block_device_t dev);
int block_dev_get_count();
void block_dev_scan_partitions(int dev_id);
kabi_block_device_t* block_dev_get_by_index(int index);
kabi_block_device_t* block_dev_get_by_name(const char *name);

#endif
