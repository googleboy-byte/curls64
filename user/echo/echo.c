#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        ulib_print(argv[i]);
        if (i < argc - 1) {
            ulib_print(" ");
        }
    }
    ulib_print("\n");
    uabi_exit(0);
}
