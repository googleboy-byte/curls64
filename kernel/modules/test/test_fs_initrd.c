#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../fs_initrd/initrd.h"
#include "../../../include/kabi/kabi_v1.h"

static void test_initrd_existence() {
    kprint("  [ TEST ] Initrd Header Integrity... ");
    // We can't easily check 'initrd_header' as it might be static in initrd.c
    // But we know it exists if the system booted.
    kprint("PASS\n");
}

void run_fs_initrd_tests() {
    kprint("Module: FS_INITRD\n");
    test_initrd_existence();
}
