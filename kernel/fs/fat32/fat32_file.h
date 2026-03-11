#ifndef FAT32_FILE_H
#define FAT32_FILE_H

#include <stdint.h>
#include "fat32_bpb.h"
#include "fat32_dir.h"

/**
 * Searches the root directory for a file with the given name.
 * The name should be a simple filename (no paths).
 * Returns KABI_SUCCESS if found, or an error code.
 */
int fat32_find_file(uint32_t dev, 
                   fat32_mount_t *m, 
                   const char *name, 
                   uint32_t start_cluster,
                   fat32_dirent_t *out_dirent);

/**
 * Reads the entire contents of a file into the provided buffer.
 * The buffer must be large enough to hold file->size bytes.
 * Returns KABI_SUCCESS or an error code.
 */
int fat32_read_file(uint32_t dev, 
                   fat32_mount_t *m, 
                   fat32_dirent_t *file, 
                   uint8_t *buffer);

int fat32_add_dirent(uint32_t dev,
                    fat32_mount_t *m,
                    uint32_t dir_cluster,
                    const char *name,
                    uint32_t first_cluster,
                    uint32_t size,
                    uint8_t attr);

int fat32_update_dirent(uint32_t dev,
                       fat32_mount_t *m,
                       uint32_t dir_cluster,
                       const char *name,
                       uint32_t new_size,
                       uint32_t new_cluster);

/**
 * Creates a new subdirectory with the given name under dir_cluster.
 * Allocates a cluster for the new dir, writes '.' and '..' entries,
 * and adds a FAT_ATTR_DIR dirent to the parent.
 */
int fat32_mkdir(uint32_t dev,
               fat32_mount_t *m,
               uint32_t parent_cluster,
               const char *name);

/**
 * Marks the named entry in dir_cluster as deleted (0xE5) and
 * frees its cluster chain in the FAT.
 */
int fat32_unlink(uint32_t dev,
                fat32_mount_t *m,
                uint32_t dir_cluster,
                const char *name);

#endif
