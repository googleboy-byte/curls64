#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc > 1) {
        // Simple help for specific command
        char *cmd = argv[1];
        if (ulib_strcmp(cmd, "ls") == 0) ulib_print("ls [path] - List directory contents\n");
        else if (ulib_strcmp(cmd, "ps") == 0) ulib_print("ps - List active processes\n");
        else if (ulib_strcmp(cmd, "cat") == 0) ulib_print("cat <file> - Display file content\n");
        else if (ulib_strcmp(cmd, "top") == 0) ulib_print("top - Show system statistics\n");
        else if (ulib_strcmp(cmd, "echo") == 0) ulib_print("echo [text] - Print text to screen\n");
        else if (ulib_strcmp(cmd, "pwd") == 0) ulib_print("pwd - Print current working directory\n");
        else if (ulib_strcmp(cmd, "clear") == 0) ulib_print("clear - Clear the terminal screen\n");
        else if (ulib_strcmp(cmd, "sleep") == 0) ulib_print("sleep <ms> - Wait for N milliseconds\n");
        else if (ulib_strcmp(cmd, "touch") == 0) ulib_print("touch <file> - Create an empty file\n");
        else if (ulib_strcmp(cmd, "write") == 0) ulib_print("write <file> <text> - Overwrite file content\n");
        else if (ulib_strcmp(cmd, "write_a") == 0) ulib_print("write_a <file> <text> - Append to file content\n");
        else ulib_print("No help available for that command.\n");
    } else {
        ulib_print("Curls OS Available Commands:\n");
        ulib_print("  Built-ins: cd, exit, exec\n");
        ulib_print("  Utilities: ls, ps, cat, top, echo, pwd, clear, sleep, touch, write, write_a\n\n");
        ulib_print("All utilities are located in /BIN/\n");
    }
    uabi_exit(0);
}
