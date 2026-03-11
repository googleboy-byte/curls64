#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        ulib_print("Usage: rm <file>\n");
        uabi_exit(1);
    }

    if (uabi_unlink(argv[1]) < 0) {
        ulib_print("rm: failed to remove '");
        ulib_print(argv[1]);
        ulib_print("'\n");
        uabi_exit(1);
    }

    uabi_exit(0);
}
