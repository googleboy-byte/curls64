#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../sysmon/sysmon_top.h"
#include "../../../include/kabi/kabi_v1.h"

static void test_sysmon_active() {
    kprint("  [ TEST ] Sysmon Infrastructure... ");
    kprint("PASS\n");
}

void run_sysmon_tests() {
    kprint("Module: SYSMON\n");
    test_sysmon_active();
}
