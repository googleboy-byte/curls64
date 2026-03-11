#include <stdint.h>

extern void syscall_print(const char *s);
extern void syscall_exit();

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
            syscall_print("Usage: argtest [args...]\nPrints all received command-line arguments.\n");
            syscall_exit();
        }
    }
    syscall_print("Argtest started!\n");
    
    char s[16];
    syscall_print("argc: ");
    int_to_ascii(argc, s);
    syscall_print(s);
    syscall_print("\n");

    for (int i = 0; i < argc; i++) {
        syscall_print("argv[");
        int_to_ascii(i, s);
        syscall_print(s);
        syscall_print("]: ");
        syscall_print(argv[i]);
        syscall_print("\n");
    }

    syscall_print("Argtest exiting...\n");
    syscall_exit();
}
