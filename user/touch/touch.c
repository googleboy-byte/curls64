#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 2) {
        ulib_print("touch: missing file operand\n");
        uabi_exit(1);
    }
    
    char *path = argv[1];
    char target[256];
    char cwd[256];
    uabi_getcwd(cwd, sizeof(cwd));

    if (path[0] == '/') {
        ulib_strcpy(target, path);
    } else {
        ulib_strcpy(target, cwd);
        if (ulib_strcmp(cwd, "/") != 0) {
            ulib_strcat(target, "/");
        }
        ulib_strcat(target, path);
    }
    
    int fd = uabi_open(target, UABI_O_CREAT | UABI_O_WRONLY);
    if (fd < 0) {
        ulib_print("touch: cannot create '");
        ulib_print(path);
        ulib_print("'\n");
        uabi_exit(1);
    } else {
        uabi_close(fd);
    }
    uabi_exit(0);
}
