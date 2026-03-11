#ifndef FAT32_BPB_H
#define FAT32_BPB_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    uint8_t  oem[8];

    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;

    // FAT32 extended
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;

    uint8_t  reserved[12];
} fat32_bpb_t;

typedef struct {
    uint32_t part_lba_start;

    uint32_t fat_lba_start;
    uint32_t data_lba_start;

    uint32_t sectors_per_cluster;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_fat;

    uint32_t root_cluster;
} fat32_mount_t;

/**
 * Reads and parses the FAT32 BPB from the specified partition.
 * Returns KABI_SUCCESS on success, error code otherwise.
 */
int fat32_read_bpb(uint32_t dev,
                   uint32_t part_lba,
                   fat32_mount_t *out);

/**
 * Helper to convert a FAT32 cluster number to its absolute LBA.
 */
static inline uint32_t fat32_cluster_to_lba(
    fat32_mount_t *m,
    uint32_t cluster)
{
    return m->data_lba_start +
        (cluster - 2) * m->sectors_per_cluster;
}

#endif
