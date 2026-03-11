#include "initrd.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"

// Core structures (opaque to module via K-ABI normally, but same-binary here)
#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_PERSISTENT  0x2000

typedef struct fs_node {
    char name[128];
    uint32_t mask, uid, gid, flags, inode, length;
    void *impl;
    void *read, *write, *open, *close, *readdir, *finddir;
    struct fs_node *ptr;
} fs_node_t;

initrd_header_t *initrd_header;
initrd_file_header_t *file_headers;
fs_node_t *initrd_root;
fs_node_t *root_nodes;
int nroot_nodes;

kabi_dirent_t shared_dirent;

static uint32_t initrd_read(kabi_fs_node_t *node_ptr, uint32_t offset, uint32_t size, uint8_t *buffer) {
    fs_node_t *node = (fs_node_t*)node_ptr;
    initrd_file_header_t header = file_headers[node->inode];
    if (offset > header.length) return 0;
    if (offset+size > header.length) size = header.length-offset;
    memory_copy((uint8_t*)(header.offset+offset), buffer, size);
    return size;
}

static kabi_dirent_t *initrd_readdir(kabi_fs_node_t *node_ptr, uint32_t index) {
    fs_node_t *node = (fs_node_t*)node_ptr;
    if ((node->flags & FS_DIRECTORY) && index < (uint32_t)nroot_nodes) {
        strcpy(shared_dirent.name, root_nodes[index].name);
        shared_dirent.ino = root_nodes[index].inode;
        shared_dirent.size = root_nodes[index].length;
        shared_dirent.type = FS_FILE; // Initrd is a flat list of files
        shared_dirent.attr = 0; // Standard archive, no special attributes
        return &shared_dirent;
    }
    return 0;
}

static kabi_fs_node_t *initrd_finddir(kabi_fs_node_t *node_ptr, char *name) {
    fs_node_t *node = (fs_node_t*)node_ptr;
    if (node->flags & FS_DIRECTORY) {
        for (int i = 0; i < nroot_nodes; i++)
            if (!strcmp(name, root_nodes[i].name))
                return (kabi_fs_node_t*)&root_nodes[i];
    }
    return 0;
}

static kabi_fs_ops_t initrd_ops = {
    .read = initrd_read,
    .write = 0,
    .open = 0,
    .close = 0,
    .readdir = initrd_readdir,
    .finddir = initrd_finddir
};

extern unsigned char initrd_bin[];

void fs_initrd_init() {
    uint32_t location = (uint32_t)initrd_bin;
    initrd_header = (initrd_header_t *)location;
    file_headers = (initrd_file_header_t *)(location + sizeof(initrd_header_t));

    initrd_root = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
    memory_set((uint8_t*)initrd_root, 0, sizeof(fs_node_t));
    strcpy(initrd_root->name, "initrd");
    initrd_root->flags = FS_DIRECTORY | FS_PERSISTENT;
    initrd_root->readdir = (void*)initrd_readdir;
    initrd_root->finddir = (void*)initrd_finddir;

    root_nodes = (fs_node_t*)kmalloc(sizeof(fs_node_t) * initrd_header->nfiles, 0, 0);
    nroot_nodes = initrd_header->nfiles;

    for (int i = 0; i < nroot_nodes; i++) {
        file_headers[i].offset += location;
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
