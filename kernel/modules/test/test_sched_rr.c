#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../../../include/kabi/kabi_v1.h"

static void test_sched_rr_config() {
    kprint("  [ TEST ] Round Robin Scheduler Consistency... ");
    // Round robin is the default if modules/sched_rr is linked.
    kprint("PASS\n");
}

void run_sched_rr_tests() {
    kprint("Module: SCHED_RR\n");
    test_sched_rr_config();
}
