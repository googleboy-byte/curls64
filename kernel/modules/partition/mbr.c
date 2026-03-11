#include "mbr.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"

int mbr_scan(uint32_t dev, kabi_partition_t *out, int max_parts) {
    uint8_t buffer[512];
    
    if (!out || max_parts <= 0) return KABI_EINVAL;

    // 1. Read LBA 0 (MBR)
    if (kabi_block_read(dev, 0, buffer) != KABI_SUCCESS) {
        return KABI_EIO;
    }

    mbr_t *mbr = (mbr_t *)buffer;

    // 2. Validate Signature
    if (mbr->signature != MBR_SIGNATURE) {
        goto fallback;
    }

    // Heuristic: If it has 0xEB/0xE9 and specific FAT strings, it might be a BPB
    // misidentified as an MBR because of the 0xAA55 signature.
    if ((buffer[0] == 0xEB || buffer[0] == 0xE9)) {
        // Check for "FAT" in the BPB area (offset 54 or 82)
        if (memory_compare(buffer + 54, "FAT", 3) == 0 || 
            memory_compare(buffer + 82, "FAT", 3) == 0) {
            goto fallback;
        }
    }

    // 3. Scan Partition Table
    int found_count = 0;
    for (int i = 0; i < 4 && found_count < max_parts; i++) {
        partition_entry_t *p = &mbr->partitions[i];
        
        if (p->type != 0 && p->sector_count > 0) {
            out[found_count].start_lba = p->start_lba;
            out[found_count].sector_count = p->sector_count;
            out[found_count].type = p->type;
            out[found_count].index = i + 1;
            out[found_count].found = 1;
            found_count++;
        }
    }

    return found_count;

fallback:
    // Heuristic: Byte 0 is often 0xEB or 0xE9 for x86 bootable disks (BPB)
    if (buffer[0] == 0xEB || buffer[0] == 0xE9) {
        out[0].start_lba = 0;
        out[0].sector_count = 0;
        out[0].type = PARTITION_TYPE_FAT32_LBA;
        out[0].index = 0;
        out[0].found = 1;
        return 1;
    }
    return KABI_ENOENT;
}

int mbr_find_fat32(uint32_t dev, kabi_partition_t *out) {
    kabi_partition_t parts[4];
    int count = mbr_scan(dev, parts, 4);
    if (count < 0) return count;

    for (int i = 0; i < count; i++) {
        if (parts[i].type == PARTITION_TYPE_FAT32_CHS || parts[i].type == PARTITION_TYPE_FAT32_LBA) {
            *out = parts[i];
            return KABI_SUCCESS;
        }
    }

    return KABI_ENOENT;
}

/* --- Module Registration --- */
kabi_module_t __kabi_module_mbr = {
    .name           = "mbr",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = NULL,
    .exit           = NULL,
    .description    = "MBR partition table parser"
};
