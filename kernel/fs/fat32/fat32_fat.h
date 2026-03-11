#ifndef FAT32_FAT_H
#define FAT32_FAT_H

#include <stdint.h>
#include "fat32_bpb.h"

/**
 * Returns the next cluster number in the chain for a given cluster.
 * Returns 0x0FFFFFFF if the end of chain is reached or an error occurs.
 */
uint32_t fat32_get_next_cluster(
    uint32_t dev,
    fat32_mount_t *m,
    uint32_t cluster);

void fat32_set_cluster(
    uint32_t dev,
    fat32_mount_t *m,
    uint32_t cluster,
    uint32_t value);

uint32_t fat32_find_free_cluster(
    uint32_t dev,
    fat32_mount_t *m);

#endif
