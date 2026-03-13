#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../../../include/kabi/kabi_v1.h"

void run_all_module_tests() {
    kprint("\n--- [ MODULE TESTS ] Starting Comprehensive Suite ---\n");

    kprint("[ SKIP ] Drivers tests (Not ported to x64)\n");
    kprint("[ SKIP ] USB tests (Not ported to x64)\n");
    kprint("[ SKIP ] FS Initrd tests (Not ported to x64)\n");
    kprint("[ SKIP ] Partition tests (Not ported to x64)\n");
    kprint("[ SKIP ] Sched RR tests (Not ported to x64)\n");
    kprint("[ SKIP ] Sysmon tests (Not ported to x64)\n");

    kprint("--- [ MODULE TESTS ] All Module Tests Complete ---\n\n");
}
