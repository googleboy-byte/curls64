#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

#ifdef ARCH_X86_64
void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    ulib_print("Init process started!\n");
    char *args[] = {"/BIN/SH.ELF", 0};
    uabi_exec("/BIN/SH.ELF", args);
    while (1) {}
}
#else
void syscall_print(const char *msg);
void syscall_exit();

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    syscall_print("Init process started!\n");
    syscall_exit();
}
#endif
