#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void format_permissions(uint8_t type, uint8_t attr, char *out) {
    if (type == 2) out[0] = 'd';      // FS_DIRECTORY
    else if (type == 1) out[0] = '-'; // FS_FILE
    else out[0] = '?';

    out[1] = 'r';
    out[2] = (attr & 0x01) ? '-' : 'w';
    out[3] = 'x';
    out[4] = 'r';
    out[5] = (attr & 0x01) ? '-' : 'w';
    out[6] = 'x';
    out[7] = 'r';
    out[8] = (attr & 0x01) ? '-' : 'w';
    out[9] = 'x';
    out[10] = '\0';
}

static void print_padded_str(const char *str, int width, int right_align) {
    int len = ulib_strlen(str);
    if (!right_align) ulib_print(str);
    for (int i = 0; i < width - len; i++) ulib_print(" ");
    if (right_align) ulib_print(str);
}

void _start(int argc, char **argv) {
    static uabi_dirent_t entries[64];
    char target[256];
    char cwd[256];
    
    uabi_getcwd(cwd, sizeof(cwd));

    if (argc < 2) {
        ulib_strcpy(target, cwd);
    } else {
        char *path = argv[1];
        if (path[0] == '/') {
            ulib_strcpy(target, path);
        } else {
            ulib_strcpy(target, cwd);
            if (ulib_strcmp(cwd, "/") != 0) {
                ulib_strcat(target, "/");
            }
            ulib_strcat(target, path);
        }
    }

    int count = uabi_readdir(target, entries, 64);
    if (count < 0) {
        ulib_print("ls: cannot access '");
        ulib_print(target);
        ulib_print("': No such file or directory\n");
        uabi_exit(1);
    }

    for (int i = 0; i < count; i++) {
        char s[32];
        char perms[11];
        
        // Inode (width 6, right aligned)
        ulib_int_to_str(entries[i].inode, s);
        print_padded_str(s, 6, 1);
        ulib_print(" ");

        // Permissions
        format_permissions(entries[i].type, entries[i].attr, perms);
        ulib_print(perms);
        ulib_print(" ");

        // Size (width 10, right aligned)
        ulib_int_to_str(entries[i].size, s);
        print_padded_str(s, 10, 1);
        ulib_print(" ");

        // Name
        ulib_print(entries[i].name);
        ulib_print("\n");
    }

    uabi_exit(0);
}
