void syscall_print(const char *msg);
void syscall_exit();

void _start(int argc, char **argv) {
    if (argc > 1) {
        // Simple manual compare since we don't include string.h here
        const char *h = "--help";
        int match = 1;
        for (int i = 0; h[i] || argv[1][i]; i++) {
            if (h[i] != argv[1][i]) { match = 0; break; }
        }
        if (match) {
            syscall_print("Usage: hello\nPrints a friendly greeting from user mode.\n");
            syscall_exit();
        }
    }
    syscall_print("Hello from Curls OS User Mode ELF!\n");
    syscall_exit();
}
