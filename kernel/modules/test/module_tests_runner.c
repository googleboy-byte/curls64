#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../../../include/kabi/kabi_v1.h"

void run_all_module_tests() {
    kprint("\n--- [ MODULE TESTS ] Starting Comprehensive Suite ---\n");

    run_drivers_tests();
    run_usb_tests();
    run_fs_initrd_tests();
    run_partition_tests();
    run_sched_rr_tests();
    run_sysmon_tests();

    kprint("--- [ MODULE TESTS ] All Module Tests Complete ---\n\n");
}
