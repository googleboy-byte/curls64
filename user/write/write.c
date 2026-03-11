#include "../../include/module/module_abi_v1.h"
#include "../lib/ulib.h"

void _start(int argc, char **argv) {
    if (argc < 3) {
        ulib_print("Usage: write <file> <text>\n");
        uabi_exit(1);
    }
    
    char *path = argv[1];
    char *msg = argv[2];
    
    int fd = uabi_open(path, UABI_O_RDWR | UABI_O_CREAT);
    if (fd < 0) {
        ulib_print("write: ");
        ulib_print(path);
        ulib_print(": No such file or directory\n");
        uabi_exit(1);
    }
    
    // Simple escape interpretation for \n
    char processed[256];
    int j = 0;
    for (int i = 0; msg[i] && j < 255; i++) {
        if (msg[i] == '\\' && msg[i+1] == 'n') {
            processed[j++] = '\n';
            i++;
        } else {
            processed[j++] = msg[i];
        }
    }
    processed[j] = '\0';
    
    uabi_write(fd, processed, (uint32_t)ulib_strlen(processed));
    uabi_close(fd);
    uabi_exit(0);
}
