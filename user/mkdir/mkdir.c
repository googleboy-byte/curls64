#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        ulib_print("Usage: mkdir <directory>\n");
        uabi_exit(1);
    }

    if (uabi_mkdir(argv[1]) < 0) {
        ulib_print("mkdir: failed to create directory '");
        ulib_print(argv[1]);
        ulib_print("'\n");
        uabi_exit(1);
    }

    uabi_exit(0);
}
