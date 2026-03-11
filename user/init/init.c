void syscall_print(const char *msg);
void syscall_exit();

void _start(int argc, char **argv) {
    (void)argc; (void)argv;
    syscall_print("Init process started!\n");
    // Busy loop or exit for now
    syscall_exit();
}
