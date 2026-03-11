#include "fat32_file.h"
#include "fat32_fat.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"

/**
 * Internal: Simple memcmp implementation.
 */
static int fat32_memcmp(const void *s1, const void *s2, uint32_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (uint32_t i = 0; i < n; i++) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}

/**
 * Internal: Canonicalize a name to 8.3 format for comparison.
 * e.g., "test.txt" -> "TEST    TXT"
 */
static void canonicalize_83(const char *input, uint8_t output[11]) {
    memory_set(output, ' ', 11);
    int i = 0, j = 0;

    // Base name
    for (; input[i] != '.' && input[i] != '\0' && j < 8; i++, j++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        output[j] = (uint8_t)c;
    }

    // Skip to dot if present
    while (input[i] != '.' && input[i] != '\0') i++;

    if (input[i] == '.') {
        i++;
        j = 8;
        // Extension
        for (; input[i] != '\0' && j < 11; i++, j++) {
            char c = input[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            output[j] = (uint8_t)c;
        }
    }
}

int fat32_find_file(uint32_t dev, 
                   fat32_mount_t *m, 
                   const char *name, 
                   uint32_t start_cluster,
                   fat32_dirent_t *out_dirent) 
{
    if (!m || !name || !out_dirent) return KABI_EINVAL;

    uint8_t target_name[11];
    canonicalize_83(name, target_name);

    uint32_t cluster = start_cluster;
    if (cluster == 0) cluster = m->root_cluster;

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS)
                return KABI_EIO;

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;

            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) return KABI_ENOENT;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr == FAT_ATTR_LFN) continue;

                if (fat32_memcmp(entries[i].name, target_name, 11) == 0) {
                    memory_copy((uint8_t *)&entries[i], (uint8_t *)out_dirent, sizeof(fat32_dirent_t));
                    return KABI_SUCCESS;
                }
            }
        }

        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return KABI_ENOENT;
}

int fat32_read_file(uint32_t dev, 
                   fat32_mount_t *m, 
                   fat32_dirent_t *file, 
                   uint8_t *buffer) 
{
    if (!m || !file || !buffer) return KABI_EINVAL;

    uint32_t cluster = (file->cluster_hi << 16) | file->cluster_lo;
    uint32_t bytes_remaining = file->size;
    uint32_t buffer_offset = 0;

    while (bytes_remaining > 0 && cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        for (uint32_t s = 0; s < m->sectors_per_cluster && bytes_remaining > 0; s++) {
            uint8_t sector_buf[512];
            if (kabi_block_read(dev, lba + s, sector_buf) != KABI_SUCCESS)
                return KABI_EIO;

            uint32_t to_copy = (bytes_remaining > 512) ? 512 : bytes_remaining;
            memory_copy(sector_buf, buffer + buffer_offset, to_copy);
            
            buffer_offset += to_copy;
            bytes_remaining -= to_copy;
        }

        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return (bytes_remaining == 0) ? KABI_SUCCESS : KABI_EIO;
}

int fat32_add_dirent(uint32_t dev,
                    fat32_mount_t *m,
                    uint32_t dir_cluster,
                    const char *name,
                    uint32_t first_cluster,
                    uint32_t size,
                    uint8_t attr) 
{
    if (!m || !name) return KABI_EINVAL;

    uint8_t target_name[11];
    canonicalize_83(name, target_name);

    uint32_t cluster = dir_cluster;
    if (cluster == 0) cluster = m->root_cluster;

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS) return KABI_EIO;

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;
            for (int i = 0; i < 16; i++) {
                // 0x00 = free, 0xE5 = deleted (reusable)
                if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                    memory_set((uint8_t*)&entries[i], 0, sizeof(fat32_dirent_t));
                    memory_copy(target_name, entries[i].name, 11);
                    entries[i].attr = attr;
                    entries[i].cluster_hi = (uint16_t)(first_cluster >> 16);
                    entries[i].cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
                    entries[i].size = size;
                    
                    // TODO: Set creation/status timestamps if we care
                    
                    if (kabi_block_write(dev, lba + s, buf) != KABI_SUCCESS) return KABI_EIO;
                    return KABI_SUCCESS;
                }
            }
        }

        // Directory full? Need to allocate another cluster for the directory itself.
        // For now, let's just return an error if the directory is full.
        uint32_t next = fat32_get_next_cluster(dev, m, cluster);
        if (next >= 0x0FFFFFF8) {
            // Simple allocation for directory expansion
            uint32_t new_c = fat32_find_free_cluster(dev, m);
            if (new_c == 0) return KABI_ENOMEM;
            
            fat32_set_cluster(dev, m, cluster, new_c);
            fat32_set_cluster(dev, m, new_c, 0x0FFFFFFF);
            
            // Zero the new cluster
            uint8_t zero_buf[512];
            memory_set(zero_buf, 0, 512);
            uint32_t new_lba = fat32_cluster_to_lba(m, new_c);
            for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
                kabi_block_write(dev, new_lba + s, zero_buf);
            }
            
            cluster = new_c;
            // Loop will continue and use the first slot in the new cluster
        } else {
            cluster = next;
        }
    }

    return KABI_EIO;
}

int fat32_update_dirent(uint32_t dev,
                       fat32_mount_t *m,
                       uint32_t dir_cluster,
                       const char *name,
                       uint32_t new_size,
                       uint32_t new_cluster) 
{
    if (!m || !name) return KABI_EINVAL;

    uint8_t target_name[11];
    canonicalize_83(name, target_name);

    uint32_t cluster = dir_cluster;
    if (cluster == 0) cluster = m->root_cluster;

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS) return KABI_EIO;

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) return KABI_ENOENT;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr == FAT_ATTR_LFN) continue;

                if (fat32_memcmp(entries[i].name, target_name, 11) == 0) {
                    if (new_size != 0xFFFFFFFF) entries[i].size = new_size;
                    if (new_cluster != 0xFFFFFFFF) {
                        entries[i].cluster_hi = (uint16_t)(new_cluster >> 16);
                        entries[i].cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
                    }
                    if (kabi_block_write(dev, lba + s, buf) != KABI_SUCCESS) return KABI_EIO;
                    return KABI_SUCCESS;
                }
            }
        }
        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return KABI_ENOENT;
}

int fat32_mkdir(uint32_t dev,
               fat32_mount_t *m,
               uint32_t parent_cluster,
               const char *name)
{
    if (!m || !name) return KABI_EINVAL;
    if (parent_cluster == 0) parent_cluster = m->root_cluster;

    // Check if it already exists
    fat32_dirent_t existing;
    if (fat32_find_file(dev, m, name, parent_cluster, &existing) == KABI_SUCCESS)
        return KABI_EINVAL; // Already exists

    // Allocate a cluster for the new directory
    uint32_t new_cluster = fat32_find_free_cluster(dev, m);
    if (new_cluster == 0) return KABI_ENOMEM;
    fat32_set_cluster(dev, m, new_cluster, 0x0FFFFFFF); // Mark as end-of-chain

    // Zero-fill the new cluster
    uint8_t zero_buf[512];
    memory_set(zero_buf, 0, 512);
    uint32_t new_lba = fat32_cluster_to_lba(m, new_cluster);
    for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
        kabi_block_write(dev, new_lba + s, zero_buf);

    // Write '.' and '..' entries into the first sector of the new cluster
    uint8_t dot_buf[512];
    memory_set(dot_buf, 0, 512);
    fat32_dirent_t *entries = (fat32_dirent_t *)dot_buf;

    // '.' entry — points to itself
    memory_set(entries[0].name, ' ', 11);
    entries[0].name[0] = '.';
    entries[0].attr = FAT_ATTR_DIR;
    entries[0].cluster_hi = (uint16_t)(new_cluster >> 16);
    entries[0].cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    entries[0].size = 0;

    // '..' entry — points to parent
    memory_set(entries[1].name, ' ', 11);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attr = FAT_ATTR_DIR;
    entries[1].cluster_hi = (uint16_t)(parent_cluster >> 16);
    entries[1].cluster_lo = (uint16_t)(parent_cluster & 0xFFFF);
    entries[1].size = 0;

    if (kabi_block_write(dev, new_lba, dot_buf) != KABI_SUCCESS) return KABI_EIO;

    // Add a DIR dirent in the parent directory
    return fat32_add_dirent(dev, m, parent_cluster, name, new_cluster, 0, FAT_ATTR_DIR);
}

int fat32_unlink(uint32_t dev,
                fat32_mount_t *m,
                uint32_t dir_cluster,
                const char *name)
{
    if (!m || !name) return KABI_EINVAL;
    if (dir_cluster == 0) dir_cluster = m->root_cluster;

    uint8_t target_name[11];
    uint8_t canon[11];
    // Reuse the same canonicalize logic inline
    memory_set(canon, ' ', 11);
    int i = 0, j = 0;
    for (; name[i] != '.' && name[i] != '\0' && j < 8; i++, j++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        canon[j] = (uint8_t)c;
    }
    while (name[i] != '.' && name[i] != '\0') i++;
    if (name[i] == '.') {
        i++; j = 8;
        for (; name[i] != '\0' && j < 11; i++, j++) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            canon[j] = (uint8_t)c;
        }
    }
    memory_copy(canon, target_name, 11);

    uint32_t cluster = dir_cluster;
    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);
        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS) return KABI_EIO;

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;
            for (int k = 0; k < 16; k++) {
                if (entries[k].name[0] == 0x00) return KABI_ENOENT;
                if (entries[k].name[0] == 0xE5) continue;
                if (entries[k].attr == FAT_ATTR_LFN) continue;

                int match = 1;
                for (int n = 0; n < 11; n++) {
                    if (entries[k].name[n] != target_name[n]) { match = 0; break; }
                }
                if (match) {
                    // Free the cluster chain
                    uint32_t fc = ((uint32_t)entries[k].cluster_hi << 16) | entries[k].cluster_lo;
                    while (fc && fc < 0x0FFFFFF8) {
                        uint32_t next = fat32_get_next_cluster(dev, m, fc);
                        fat32_set_cluster(dev, m, fc, 0); // Mark as free
                        fc = next;
                    }
                    // Mark dirent as deleted
                    entries[k].name[0] = 0xE5;
                    if (kabi_block_write(dev, lba + s, buf) != KABI_SUCCESS) return KABI_EIO;
                    return KABI_SUCCESS;
                }
            }
        }
        cluster = fat32_get_next_cluster(dev, m, cluster);
    }
    return KABI_ENOENT;
}
