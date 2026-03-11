#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 3) {
        ulib_print("Usage: mount <device> <mountpoint>\n");
        ulib_print("  Example: mount usb0 /usb\n");
        uabi_exit(1);
    }

    char *device = argv[1];
    char *mountpoint = argv[2];

    int rc = uabi_mount(device, mountpoint);
    if (rc == 0) {
        ulib_print("Mounted ");
        ulib_print(device);
        ulib_print(" at ");
        ulib_print(mountpoint);
        ulib_print("\n");
    } else {
        ulib_print("mount: failed to mount ");
        ulib_print(device);
        ulib_print(" at ");
        ulib_print(mountpoint);
        ulib_print("\n");
    }

    uabi_exit(rc == 0 ? 0 : 1);
}
