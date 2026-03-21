#include <stdint.h>
#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void int_to_ascii(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) n = -n;
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0) str[i++] = '-';
    str[i] = '\0';

    // Reverse string
    int j, k;
    char c;
    for (j = 0, k = i-1; j < k; j++, k--) {
        c = str[j];
        str[j] = str[k];
        str[k] = c;
    }
}

void _start(int argc, char **argv) {
    if (argc > 1) {
        // Simple manual compare
        const char *h = "--help";
        int match = 1;
        for (int i = 0; h[i] || argv[1][i]; i++) {
            if (h[i] != argv[1][i]) { match = 0; break; }
        }
        if (match) {
            ulib_print("Usage: argtest [args...]\nPrints all received command-line arguments.\n");
            uabi_exit(0);
        }
    }
    ulib_print("Argtest started!\n");
    
    char s[16];
    ulib_print("argc: ");
    int_to_ascii(argc, s);
    ulib_print(s);
    ulib_print("\n");

    for (int i = 0; i < argc; i++) {
        ulib_print("argv[");
        int_to_ascii(i, s);
        ulib_print(s);
        ulib_print("]: ");
        ulib_print(argv[i]);
        ulib_print("\n");
    }

    ulib_print("Argtest exiting...\n");
    uabi_exit(0);
}
