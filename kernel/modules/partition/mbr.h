#ifndef MBR_H
#define MBR_H

#include <stdint.h>

#define MBR_SIGNATURE 0xAA55
#define PARTITION_TYPE_FAT32_CHS 0x0B
#define PARTITION_TYPE_FAT32_LBA 0x0C

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t start_chs[3];
    uint8_t type;
    uint8_t end_chs[3];
    uint32_t start_lba;
    uint32_t sector_count;
} partition_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t bootstrap[446];
    partition_entry_t partitions[4];
    uint16_t signature;
} mbr_t;

typedef struct {
    uint32_t start_lba;
    uint32_t sector_count;
    uint8_t type;
    uint8_t index; // Partition index (1-4, or 0 for whole disk)
    int found;
} kabi_partition_t;

/**
 * Scans for partitions on the specified device.
 * @param dev The block device ID to scan.
 * @param out Array of kabi_partition_t to fill.
 * @param max_parts Maximum number of partitions to find.
 * @return Number of partitions found, or negative error code.
 */
int mbr_scan(uint32_t dev, kabi_partition_t *out, int max_parts);

/**
 * Finds the first FAT32 partition on the specified device.
 * (Deprecated: use mbr_scan instead)
 */
int mbr_find_fat32(uint32_t dev, kabi_partition_t *out);

#endif
