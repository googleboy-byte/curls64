#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    char cwd[256];
    if (uabi_getcwd(cwd, sizeof(cwd)) == 0) {
        ulib_print(cwd);
        ulib_print("\n");
        uabi_exit(0);
    } else {
        ulib_print("pwd: failed to get current directory\n");
        uabi_exit(1);
    }
}
