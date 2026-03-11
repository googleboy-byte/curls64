#ifndef MODULE_TESTS_RUNNER_H
#define MODULE_TESTS_RUNNER_H

void run_all_module_tests();

/* Module test suites */
void run_drivers_tests();
void run_usb_tests();
void run_fs_initrd_tests();
void run_partition_tests();
void run_sched_rr_tests();
void run_sysmon_tests();

#endif
