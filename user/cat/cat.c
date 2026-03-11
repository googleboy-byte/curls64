#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    int fd;
    if (argc < 2) {
        fd = 0; // Standard input
    } else {
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
        
        fd = uabi_open(target, UABI_O_RDONLY);
        if (fd < 0) {
            ulib_print("cat: ");
            ulib_print(path);
            ulib_print(": No such file or directory\n");
            uabi_exit(1);
        }
    }
    
    char buf[513];
    int bytes;
    while ((bytes = uabi_read(fd, buf, 512)) > 0) {
        buf[bytes] = '\0';
        ulib_print(buf);
    }
    
    if (fd != 0) uabi_close(fd);
    uabi_exit(0);
}
