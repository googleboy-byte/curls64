#include "fat32_fat.h"
#include "../../../include/module/module_abi_v1.h"

uint32_t fat32_get_next_cluster(
    uint32_t dev,
    fat32_mount_t *m,
    uint32_t cluster)
{
    // FAT32 entries are 4 bytes each
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = m->fat_lba_start + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t buf[512];

    if (kabi_block_read(dev, fat_sector, buf) != KABI_SUCCESS)
        return 0x0FFFFFFF;

    // Mask out the top 4 bits as per FAT32 spec
    uint32_t val = *(uint32_t*)(buf + ent_offset) & 0x0FFFFFFF;

    return val;
}

void fat32_set_cluster(
    uint32_t dev,
    fat32_mount_t *m,
    uint32_t cluster,
    uint32_t value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = m->fat_lba_start + (fat_offset / 512);
    uint32_t ent_offset = fat_offset % 512;

    uint8_t buf[512];
    if (kabi_block_read(dev, fat_sector, buf) != KABI_SUCCESS) return;

    // Preserve the top 4 bits of the existing entry
    uint32_t old_val = *(uint32_t*)(buf + ent_offset);
    uint32_t new_val = (old_val & 0xF0000000) | (value & 0x0FFFFFFF);
    *(uint32_t*)(buf + ent_offset) = new_val;

    kabi_block_write(dev, fat_sector, buf);
}

uint32_t fat32_find_free_cluster(
    uint32_t dev,
    fat32_mount_t *m)
{
    // Search within FAT for a zero entry (starting from cluster 2)
    // We'll search up to the total number of clusters based on FAT size
    uint32_t entries_per_sector = 512 / 4;
    uint32_t total_fat_sectors = m->sectors_per_fat;
    
    for (uint32_t s = 0; s < total_fat_sectors; s++) {
        uint8_t buf[512];
        if (kabi_block_read(dev, m->fat_lba_start + s, buf) != KABI_SUCCESS) return 0;

        uint32_t *entries = (uint32_t*)buf;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            uint32_t cluster = (s * entries_per_sector) + i;
            if (cluster < 2) continue; // Skip first two reserved clusters
            
            if ((entries[i] & 0x0FFFFFFF) == 0) {
                return cluster;
            }
        }
    }

    return 0; // No free cluster found
}
