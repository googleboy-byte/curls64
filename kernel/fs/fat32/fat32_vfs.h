#ifndef FAT32_VFS_H
#define FAT32_VFS_H

#include <stdint.h>

/**
 * Mounts a FAT32 partition from the given device to the VFS.
 * @param dev The device ID (e.g., 0 for disk).
 * @param mountpoint The path where it should be mounted (e.g., "/").
 * @return KABI_SUCCESS or an error code.
 */
int fat32_vfs_mount(uint32_t dev, const char *mountpoint);

#endif
