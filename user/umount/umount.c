#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        ulib_print("Usage: umount <mountpoint>\n");
        ulib_print("  Example: umount /mnt\n");
        uabi_exit(1);
    }

    char *mountpoint = argv[1];

    int rc = uabi_umount(mountpoint);
    if (rc == 0) {
        ulib_print("Unmounted ");
        ulib_print(mountpoint);
        ulib_print("\n");
    } else {
        ulib_print("umount: failed to unmount ");
        ulib_print(mountpoint);
        ulib_print("\n");
    }

    uabi_exit(rc == 0 ? 0 : 1);
}
