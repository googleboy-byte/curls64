#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void resolve_path(const char *path, char *out_target) {
    char cwd[256];
    uabi_getcwd(cwd, sizeof(cwd));

    if (path[0] == '/') {
        ulib_strcpy(out_target, path);
    } else {
        ulib_strcpy(out_target, cwd);
        if (ulib_strcmp(cwd, "/") != 0) {
            ulib_strcat(out_target, "/");
        }
        ulib_strcat(out_target, path);
    }
}

void _start(int argc, char **argv) {
    if (argc < 3) {
        ulib_print("Usage: cp <source> <dest>\n");
        uabi_exit(1);
    }

    char source_path[256];
    char dest_path[256];
    resolve_path(argv[1], source_path);
    resolve_path(argv[2], dest_path);

    if (ulib_strcmp(source_path, dest_path) == 0) {
        ulib_print("cp: '");
        ulib_print(argv[1]);
        ulib_print("' and '");
        ulib_print(argv[2]);
        ulib_print("' are the same file\n");
        uabi_exit(1);
    }

    int fd_src = uabi_open(source_path, UABI_O_RDONLY);
    if (fd_src < 0) {
        ulib_print("cp: cannot open '");
        ulib_print(argv[1]);
        ulib_print("': No such file or directory\n");
        uabi_exit(1);
    }

    int fd_dest = uabi_open(dest_path, UABI_O_WRONLY | UABI_O_CREAT | UABI_O_TRUNC);
    if (fd_dest < 0) {
        ulib_print("cp: cannot create '");
        ulib_print(argv[2]);
        ulib_print("'\n");
        uabi_close(fd_src);
        uabi_exit(1);
    }

    char buf[512];
    int bytes_read;
    int success = 1;
    while ((bytes_read = uabi_read(fd_src, buf, sizeof(buf))) > 0) {
        int bytes_written = uabi_write(fd_dest, buf, bytes_read);
        if (bytes_written < bytes_read) {
            ulib_print("cp: error writing to '");
            ulib_print(argv[2]);
            ulib_print("'\n");
            success = 0;
            break;
        }
    }

    if (bytes_read < 0) {
        ulib_print("cp: error reading from '");
        ulib_print(argv[1]);
        ulib_print("'\n");
        success = 0;
    }

    uabi_close(fd_src);
    uabi_close(fd_dest);
    
    if (!success) uabi_exit(1);
    uabi_exit(0);
}
