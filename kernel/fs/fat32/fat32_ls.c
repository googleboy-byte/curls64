#include "fat32_bpb.h"
#include "fat32_fat.h"
#include "fat32_dir.h"
#include "../../../include/module/module_abi_v1.h"

/**
 * Format the 8.3 name into a human-readable string.
 */
static void format_fat_name(uint8_t name[11], char *out) {
    int p = 0;

    // Base name
    for (int i = 0; i < 8 && name[i] != ' '; i++) {
        out[p++] = name[i];
    }

    // Extension
    if (name[8] != ' ') {
        out[p++] = '.';
        for (int i = 8; i < 11 && name[i] != ' '; i++) {
            out[p++] = name[i];
        }
    }

    out[p] = 0;
}

/**
 * Lists the contents of the root directory.
 */
void fat32_ls_root(uint32_t dev, fat32_mount_t *m) {
    uint32_t cluster = m->root_cluster;

    // Loop through the cluster chain
    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        // Loop through sectors in the cluster
        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS) {
                return;
            }

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;

            // Loop through directory entries in the sector (16 per sector)
            for (int i = 0; i < 512 / sizeof(fat32_dirent_t); i++) {
                fat32_dirent_t *e = &entries[i];

                // End of directory
                if (e->name[0] == 0x00) {
                    return;
                }

                // Skip deleted entries
                if (e->name[0] == 0xE5) {
                    continue;
                }

                // Skip Long File Name entries
                if (e->attr == FAT_ATTR_LFN) {
                    continue;
                }

                // Format and print name
                char name_str[13];
                format_fat_name(e->name, name_str);
                kprint(name_str);

                if (e->attr & FAT_ATTR_DIR) {
                    kprint("/");
                }
                kprint("\n");
            }
        }

        // Move to the next cluster in the chain
        cluster = fat32_get_next_cluster(dev, m, cluster);
    }
}
