#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        ulib_print("Usage: sleep <ms>\n");
        uabi_exit(1);
    }
    char *arg = argv[1];
    uint32_t ms = 0;
    for (int i = 0; arg[i] >= '0' && arg[i] <= '9'; i++)
        ms = ms * 10 + (uint32_t)(arg[i] - '0');
    if (ms == 0 && arg[0] != '0') {
        ulib_print("sleep: invalid duration\n");
        uabi_exit(1);
    }
    uabi_sleep(ms);
    uabi_exit(0);
}
