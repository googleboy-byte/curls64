#include <stdint.h>
#include "../../include/uabi/uabi_v2.h"

void _start(int argc, char **argv) {
    uabi_print("Hello from 64-bit Userland!\n");
    
    // Test argc/argv
    uabi_print("Arg count: ");
    char s[4];
    s[0] = (char)('0' + (argc % 10));
    s[1] = '\n';
    s[2] = '\0';
    uabi_print(s);

    if (argc > 0) {
        uabi_print("Arg 0: ");
        uabi_print(argv[0]);
        uabi_print("\n");
    }

    uabi_print("Sysexit-ing...\n");
    uabi_exit(0);
}
