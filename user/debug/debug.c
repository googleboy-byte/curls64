#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        ulib_print("System debug is ");
        ulib_print(uabi_debug_enabled() ? "ON\n" : "OFF\n");
        uabi_exit(0);
    }

    if (ulib_strcmp(argv[1], "on") == 0) {
        uabi_set_debug(1);
    } else if (ulib_strcmp(argv[1], "off") == 0) {
        uabi_set_debug(0);
    } else {
        ulib_print("Usage: debug [on/off]\n");
        uabi_exit(1);
    }
    uabi_exit(0);
}
