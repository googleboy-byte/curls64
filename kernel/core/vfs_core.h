#ifndef VFS_CORE_H
#define VFS_CORE_H

#include <stdint.h>
#include <stddef.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08 // Is the file an active mountpoint?
#define FS_TRANSIENT   0x10 // Is the node transient (needs kfree)?
#define FS_IMPL_HEAP   0x20 // node->impl is a heap pointer (safe to kfree)
#define FS_PERSISTENT  0x2000 // Cannot be freed/closed by tasks
#define FS_PIPE        0x1000 // special fs node type for pipe
#define MAX_FD 32

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x40
#define O_TRUNC 0x200

#define FIRST_USER_FD 3

struct fs_node;

typedef struct {
    struct fs_node *node;
    uint32_t offset;
    uint32_t flags;
    uint32_t refcount;
} file_t;

typedef uint32_t (*read_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*, uint32_t);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*, char *name);
typedef void (*create_type_t)(struct fs_node*, char *name, uint16_t mask);
typedef void (*mkdir_type_t)(struct fs_node*, char *name, uint16_t mask);
typedef int  (*unlink_type_t)(struct fs_node*, char *name);

typedef struct fs_node {
    char name[128];     // The filename.
    uint32_t mask;       // The permissions mask.
    uint32_t uid;        // The owning user.
    uint32_t gid;        // The owning group.
    uint32_t flags;      // Includes the node type. See #defines above.
    uint32_t inode;      // This is device-specific - provides a way for a filesystem to identify files.
    uint32_t length;     // Size of the file, in bytes.
    void *impl;          // An implementation-defined pointer.
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    create_type_t create;
    mkdir_type_t mkdir;
    unlink_type_t unlink;
    struct fs_node *ptr; // Used by mountpoints and symlinks.
} fs_node_t;

#include "../../include/kabi/kabi_v1.h"

void kabi_vfs_register(kabi_fs_ops_t *ops, const char *mountpoint);
void vfs_canonicalize_path(char *dest, const char *src);
fs_node_t *vfs_resolve_path(const char *path);

struct dirent {
    char name[128]; // Filename.
    uint32_t ino;     // Inode number. Required by POSIX.
    uint32_t size;
    uint8_t type;
    uint8_t attr;
};

extern fs_node_t *fs_root; // The root of the filesystem.
extern fs_node_t *std_node; // Singleton for stdin/stdout/stderr

void init_fs();

// Standard read/write/open/close functions. Note that these are all just wrappers!
uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_fs(fs_node_t *node, uint8_t read, uint8_t write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);

// FD Level Functions
file_t *file_create(fs_node_t *node, uint32_t flags);
int open(const char *name, int flags);
int close(int fd);
int read(int fd, char *buf, int size);
int write(int fd, const char *buf, int size);
int seek(int fd, int offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int sys_mkdir(const char *path);
int sys_unlink(const char *path);
int sys_umount(const char *path);

#endif // VFS_CORE_H
