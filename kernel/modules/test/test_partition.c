#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../partition/mbr.h"
#include "../../../include/kabi/kabi_v1.h"

static void test_mbr_fat32_search() {
    kprint("  [ TEST ] MBR FAT32 Partition Discovery... ");
    kabi_partition_t part;
    // dev 0 is usually the hard drive (hda)
    int res = mbr_find_fat32(0, &part);
    if (res == KABI_SUCCESS && part.found) {
        kprint("PASS (Found FAT32 Type ");
        char buf[8]; kabi_hex_to_ascii(part.type, buf); kprint(buf);
        kprint(")\n");
    } else {
        kprint("FAIL (No FAT32 partition found on IDE 0)\n");
    }
}

void run_partition_tests() {
    kprint("Module: PARTITION (MBR)\n");
    test_mbr_fat32_search();
}
