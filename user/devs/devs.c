#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    (void)argc; (void)argv;

    int count = uabi_devinfo(-1, (uabi_devinfo_t *)0);
    if (count <= 0) {
        ulib_print("No block devices found.\n");
        uabi_exit(1);
    }

    ulib_print("Block devices:\n");
    for (int i = 0; i < count; i++) {
        uabi_devinfo_t info;
        if (uabi_devinfo(i, &info) == 0) {
            char s[32];
            if (info.is_partition) {
                ulib_print("  |- ");
            } else {
                ulib_print("  ");
            }
            ulib_print("dev");
            ulib_int_to_str(i, s);
            ulib_print(s);
            ulib_print(": ");
            ulib_print(info.name);

            if (info.sectors > 0) {
                uint32_t mb = info.sectors / 2048;
                ulib_print("  (");
                ulib_int_to_str(info.sectors, s);
                ulib_print(s);
                ulib_print(" sectors, ");
                if (mb > 0) {
                    ulib_int_to_str(mb, s);
                    ulib_print(s);
                    ulib_print(" MB");
                } else {
                    ulib_int_to_str(info.sectors / 2, s);
                    ulib_print(s);
                    ulib_print(" KB");
                }
                ulib_print(")");
            }
            ulib_print("\n");
        }
    }

    ulib_print("\nUsage: mount <device> <mountpoint>\n");
    uabi_exit(0);
}
