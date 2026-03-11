#include "fat32_bpb.h"
#include "../../../include/module/module_abi_v1.h"

int fat32_read_bpb(uint32_t dev,
                   uint32_t part_lba,
                   fat32_mount_t *out)
{
    if (!out) return KABI_EINVAL;

    uint8_t sector[512];

    // 1. Read the boot sector of the partition
    if (kabi_block_read(dev, part_lba, sector) != KABI_SUCCESS) {
        // kprint("[FAT32] Read failed\n"); // Need kprint
        return KABI_EIO;
    }

    fat32_bpb_t *bpb = (fat32_bpb_t *)sector;

    // 2. Fundamental sanity checks
    if (bpb->bytes_per_sector != 512) {
        return KABI_EINVAL;
    }

    if (bpb->fat_size_32 == 0) {
        return KABI_EINVAL;
    }

    // 3. Calculate essential offsets
    uint32_t fat_start =
        part_lba + bpb->reserved_sector_count;

    uint32_t data_start =
        fat_start + (bpb->num_fats * bpb->fat_size_32);

    // 4. Populate mount structure
    out->part_lba_start      = part_lba;
    out->fat_lba_start       = fat_start;
    out->data_lba_start      = data_start;

    out->sectors_per_cluster = bpb->sectors_per_cluster;
    out->bytes_per_sector    = bpb->bytes_per_sector;
    out->sectors_per_fat     = bpb->fat_size_32;
    out->root_cluster        = bpb->root_cluster;

    return KABI_SUCCESS;
}
