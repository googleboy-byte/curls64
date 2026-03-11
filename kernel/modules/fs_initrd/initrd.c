#include "initrd.h"
#include "../../core/vfs_core.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"

initrd_header_t *initrd_header;
initrd_file_header_t *file_headers;
fs_node_t *initrd_root;
fs_node_t *root_nodes;
int nroot_nodes;
uintptr_t initrd_location;

static uint64_t initrd_read(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (node->inode >= (uint32_t)nroot_nodes) return 0;
    initrd_file_header_t *header = &file_headers[node->inode];
    if (offset > header->length) return 0;
    if (offset + size > header->length) size = header->length - offset;
    
    // Calculate absolute physical-mapped address safely
    uintptr_t absolute_addr = initrd_location + header->offset + offset;
    memory_copy((uint8_t*)absolute_addr, buffer, size);
    return size;
}

static struct dirent *initrd_readdir(fs_node_t *node, uint64_t index) {
    if ((node->flags & FS_DIRECTORY) && index < (uint32_t)nroot_nodes) {
        static struct dirent de;
        strcpy(de.name, root_nodes[index].name);
        de.ino = root_nodes[index].inode;
        de.size = root_nodes[index].length;
        de.type = root_nodes[index].flags & 0x7;
        return &de;
    }
    return 0;
}

static fs_node_t *initrd_finddir(fs_node_t *node, char *name) {
    if (node->flags & FS_DIRECTORY) {
        for (int i = 0; i < nroot_nodes; i++)
            if (!strcmp(name, root_nodes[i].name))
                return &root_nodes[i];
    }
    return 0;
}

static kabi_fs_ops_t initrd_ops = {
    .read = (void*)initrd_read,
    .write = 0,
    .open = 0,
    .close = 0,
    .readdir = (void*)initrd_readdir,
    .finddir = (void*)initrd_finddir
};

extern unsigned char initrd_bin[];

void fs_initrd_init() {
    initrd_location = (uintptr_t)initrd_bin;
    initrd_header = (initrd_header_t *)initrd_location;
    file_headers = (initrd_file_header_t *)(initrd_location + sizeof(initrd_header_t));

    initrd_root = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
    memory_set((uint8_t*)initrd_root, 0, sizeof(fs_node_t));
    strcpy(initrd_root->name, "initrd");
    initrd_root->flags = FS_DIRECTORY | FS_PERSISTENT;
    initrd_root->readdir = (void*)initrd_readdir;
    initrd_root->finddir = (void*)initrd_finddir;

    root_nodes = (fs_node_t*)kmalloc(sizeof(fs_node_t) * initrd_header->nfiles, 0, 0);
    nroot_nodes = (int)initrd_header->nfiles;

    for (int i = 0; i < nroot_nodes; i++) {
        // DO NOT modify header->offset in-place (it's 32-bit)
        strcpy(root_nodes[i].name, file_headers[i].name);
        root_nodes[i].length = file_headers[i].length;
        root_nodes[i].inode = i;
        root_nodes[i].flags = FS_FILE | FS_PERSISTENT;
        root_nodes[i].read = (void*)initrd_read;
        kprint("    - Initrd File: "); kprint(root_nodes[i].name); kprint("\n");
    }

    kabi_vfs_register(&initrd_ops, "/");
}

/* --- Module Registration --- */
static int initrd_module_init(void) { fs_initrd_init(); return 0; }

kabi_module_t __kabi_module_initrd = {
    .name           = "fs_initrd",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = initrd_module_init,
    .exit           = NULL,
    .description    = "Initial ramdisk filesystem"
};
