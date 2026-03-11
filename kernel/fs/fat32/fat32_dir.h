#ifndef FAT32_DIR_H
#define FAT32_DIR_H

#include <stdint.h>

/**
 * FAT32 Directory Entry (32 bytes)
 */
typedef struct __attribute__((packed)) {
    uint8_t  name[11];      // 8.3 filename
    uint8_t  attr;          // Attributes
    uint8_t  ntres;         // Reserved for NT
    uint8_t  crt_tenth;     // Creation time (tenths of a second)
    uint16_t crt_time;      // Creation time
    uint16_t crt_date;      // Creation date
    uint16_t acc_date;      // Last access date
    uint16_t cluster_hi;    // High 16 bits of cluster number
    uint16_t wrt_time;      // Last write time
    uint16_t wrt_date;      // Last write date
    uint16_t cluster_lo;    // Low 16 bits of cluster number
    uint32_t size;          // File size in bytes
} fat32_dirent_t;

#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIR       0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F // Long File Name attribute

#endif
