#include "vfs_core.h"
#include <cpu_local.h>
#include "task.h"
#include "../../libc/mem.h"
#include "../../libc/string.h"
#include "pipe.h"

#include "../../include/kabi/kabi_v1.h"

fs_node_t *fs_root = 0; // The root of the filesystem.
fs_node_t *std_node = 0; // Singleton for stdin/stdout/stderr


/* ── Mount Table ─────────────────────────────────────────── */
#define MAX_MOUNTS 8
typedef struct {
    char path[128];      /* e.g. "/usb" */
    fs_node_t *root;     /* root node of mounted FS */
    int active;
} vfs_mount_t;
static vfs_mount_t mount_table[MAX_MOUNTS];
static int num_mounts = 0;

static uint64_t std_read(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    // For now, we use the existing blocking get_line
    // This is simple but works for the current shell model
    char temp[256];
    kabi_get_line(temp);
    uint64_t len = strlen(temp);
    if (len > size) len = size;
    memory_copy((uint8_t*)temp, buffer, len);
    return len;
}

static uint64_t std_write(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    // kprint expects a null-terminated string, but buffer might not be.
    // We create a temporary safe copy.
    char *safe = (char*)kmalloc(size + 1, 0, 0);
    memory_copy(buffer, (uint8_t*)safe, size);
    safe[size] = '\0';
    kprint(safe);
    kfree(safe);
    return size;
}

static uint64_t mock_read(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (!node->impl) return 0;
    uint8_t *data = node->impl;
    if (offset >= 1024) return 0;
    if (offset + size > 1024) size = 1024 - offset;
    memory_copy(data + offset, buffer, size);
    return size;
}

static uint64_t mock_write(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (!node->impl) {
        node->impl = kmalloc(1024, 0, 0);
        memory_set(node->impl, 0, 1024);
    }
    uint8_t *data = node->impl;
    if (offset >= 1024) return 0;
    if (offset + size > 1024) size = 1024 - offset;
    memory_copy(buffer, data + offset, size);
    return size;
}

void init_fs() {
    std_node = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
    memory_set((uint8_t*)std_node, 0, sizeof(fs_node_t));
    strcpy(std_node->name, "stdio");
    std_node->flags = FS_CHARDEVICE | FS_PERSISTENT;
    std_node->read = std_read;
    std_node->write = std_write;
}

void vfs_init_standard_fds(void *task_ptr) {
    task_t *t = (task_t*)task_ptr;
    if (!std_node) return;
    
    // FD 0: stdin (Read only)
    t->fd_table[0] = file_create(std_node, O_RDONLY);
    // FD 1: stdout (Write only)
    t->fd_table[1] = file_create(std_node, O_WRONLY);
    // FD 2: stderr (Write only)
    t->fd_table[2] = file_create(std_node, O_WRONLY);
}

void kabi_vfs_register(kabi_fs_ops_t *ops, const char *mountpoint) {
    if (!ops || !mountpoint) return;

    fs_node_t *node = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
    memory_set((uint8_t*)node, 0, sizeof(fs_node_t));
    strcpy(node->name, mountpoint);
    node->flags = FS_DIRECTORY | FS_PERSISTENT | FS_MOUNTPOINT;
    
    node->read = (read_type_t)ops->read;
    node->write = (write_type_t)ops->write;
    node->open = (open_type_t)ops->open;
    node->close = (close_type_t)ops->close;
    node->readdir = (readdir_type_t)ops->readdir;
    node->finddir = (finddir_type_t)ops->finddir;
    node->create = (create_type_t)ops->create;
    node->mkdir  = (mkdir_type_t)ops->mkdir;
    node->unlink = (unlink_type_t)ops->unlink;

    if (strcmp((char*)mountpoint, "/") == 0 || fs_root == 0) {
        fs_root = node;
    } else {
        /* Register as a sub-mount in the mount table */
        /* Check if already mounted at this path — replace it */
        for (int i = 0; i < num_mounts; i++) {
            if (mount_table[i].active && strcmp(mount_table[i].path, mountpoint) == 0) {
                mount_table[i].root = node;
                return;
            }
        }
        if (num_mounts < MAX_MOUNTS) {
            strcpy(mount_table[num_mounts].path, mountpoint);
            mount_table[num_mounts].root = node;
            mount_table[num_mounts].active = 1;
            num_mounts++;
        }
    }
}

int sys_umount(const char *path) {
    if (!path) return -1;
    char clean[128];
    vfs_canonicalize_path(clean, path);

    for (int i = 0; i < num_mounts; i++) {
        if (mount_table[i].active && strcmp(mount_table[i].path, clean) == 0) {
            mount_table[i].active = 0;
            return 0;
        }
    }
    return -1;
}

uint64_t read_fs(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (node && node->read != 0)
        return node->read(node, offset, size, buffer);
    return 0;
}

uint64_t write_fs(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    if (node && node->write != 0)
        return node->write(node, offset, size, buffer);
    return 0;
}

void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
    if (node && node->open != 0)
        node->open(node);
}

void close_fs(fs_node_t *node) {
    if (node && node->close != 0)
        node->close(node);
}

struct dirent *readdir_fs(fs_node_t *node, uint64_t index) {
    if (node && (node->flags & 0x7) == FS_DIRECTORY && node->readdir != 0)
        return node->readdir(node, index);
    return 0;
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
    if (node && (node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0)
        return node->finddir(node, name);
    return 0;
}

void create_fs(fs_node_t *node, char *name, uint16_t mask) {
    if (node && (node->flags & 0x7) == FS_DIRECTORY && node->create != 0)
        node->create(node, name, mask);
}

// --- FD Level Functions ---
file_t *file_create(fs_node_t *node, uint32_t flags) {
    file_t *f = (file_t*)kmalloc(sizeof(file_t), 0, 0);
    f->node = node;
    f->offset = 0;
    f->flags = flags;
    f->refcount = 1;
    return f;
}

// Helper to canonicalize paths (resolve . and ..)
void vfs_canonicalize_path(char *dest, const char *src) {
    if (!src || src[0] == '\0') {
        strcpy(dest, "/");
        return;
    }

    char buf[512];
    uint32_t buf_len = 0;

    // Handle relative vs absolute
    if (src[0] != '/') {
        if (current_task) {
            uint32_t cwd_len = strlen((char*)current_task->cwd);
            if (cwd_len >= 511) cwd_len = 511;
            memory_copy((uint8_t*)current_task->cwd, (uint8_t*)buf, cwd_len);
            buf_len = cwd_len;
            if (buf[buf_len - 1] != '/') {
                buf[buf_len++] = '/';
            }
        } else {
            buf[buf_len++] = '/';
        }
    }

    // Append src to buf with limit
    uint32_t src_len = strlen(src);
    for (uint32_t i = 0; i < src_len && buf_len < 511; i++) {
        buf[buf_len++] = src[i];
    }
    buf[buf_len] = '\0';

    char *components[32];
    int count = 0;
    char *p = buf;
    if (*p == '/') p++;
    char *token = p;

    while (1) {
        if (*p == '/' || *p == '\0') {
            char end = *p;
            *p = '\0';

            if (strcmp(token, "..") == 0) {
                if (count > 0) count--;
            } else if (strcmp(token, ".") == 0 || strlen(token) == 0) {
                // Ignore
            } else {
                if (count < 32) components[count++] = token;
            }

            if (end == '\0') break;
            token = p + 1;
            p = token;
        } else {
            p++;
        }
    }

    // Reconstruct into dest
    dest[0] = '/';
    dest[1] = '\0';
    uint32_t dest_p = 1;

    for (int i = 0; i < count; i++) {
        uint32_t comp_len = strlen(components[i]);
        // Safety: dest is assumed to be at least 512 bytes based on VFS headers
        if (dest_p + comp_len + 1 >= 512) break; 
        
        memory_copy((uint8_t*)components[i], (uint8_t*)&dest[dest_p], comp_len);
        dest_p += comp_len;
        if (i < count - 1) {
            dest[dest_p++] = '/';
        }
        dest[dest_p] = '\0';
    }
}

static void vfs_get_parent_path(char *dest, const char *path) {
    char clean[512];
    vfs_canonicalize_path(clean, path);
    int len = strlen(clean);
    if (len <= 1) { // Root or empty
        strcpy(dest, "/");
        return;
    }
    char *last_slash = 0;
    for (int i = 0; i < len; i++) if (clean[i] == '/') last_slash = &clean[i];
    
    if (last_slash == clean) {
        strcpy(dest, "/");
    } else {
        int dlen = last_slash - clean;
        memory_copy((uint8_t*)clean, (uint8_t*)dest, dlen);
        dest[dlen] = '\0';
    }
}

static void vfs_get_basename(char *dest, const char *path) {
    char clean[512];
    vfs_canonicalize_path(clean, path);
    char *last_slash = 0;
    int len = strlen(clean);
    for (int i = 0; i < len; i++) if (clean[i] == '/') last_slash = &clean[i];
    
    if (!last_slash) strcpy(dest, clean);
    else strcpy(dest, last_slash + 1);
}

fs_node_t *vfs_resolve_path(const char *path) {
    if (!path || !fs_root) return 0;
    
    char clean_path[512];
    vfs_canonicalize_path(clean_path, path);
    
    if (strcmp(clean_path, "/") == 0) return fs_root;

    /* ── Check mount table ──────────────────────────────────
     * If the path exactly matches a mount point, return that mount's root.
     * If the path starts with a mount prefix + '/', resolve the remainder
     * within the mounted filesystem. */
    for (int i = 0; i < num_mounts; i++) {
        if (!mount_table[i].active) continue;
        int mlen = strlen(mount_table[i].path);
        if (strcmp(clean_path, mount_table[i].path) == 0) {
            return mount_table[i].root;
        }
        if (mlen > 0 && clean_path[mlen] == '/') {
            int prefix_match = 1;
            for (int j = 0; j < mlen; j++) {
                if (clean_path[j] != mount_table[i].path[j]) { prefix_match = 0; break; }
            }
            if (prefix_match) {
                /* Resolve remainder within the mounted FS */
                char *remainder = clean_path + mlen + 1; /* skip mount path + '/' */
                fs_node_t *current = mount_table[i].root;
                char *p = remainder;
                char *token = p;
                while (*p) {
                    if (*p == '/') {
                        *p = '\0';
                        if (strlen(token) > 0) {
                            fs_node_t *next = finddir_fs(current, token);
                            if (current != mount_table[i].root && (current->flags & FS_TRANSIENT))
                                kfree(current);
                            current = next;
                            if (!current) return 0;
                        }
                        token = p + 1;
                    }
                    p++;
                }
                if (strlen(token) > 0) {
                    fs_node_t *next = finddir_fs(current, token);
                    if (current != mount_table[i].root && (current->flags & FS_TRANSIENT))
                        kfree(current);
                    current = next;
                }
                return current;
            }
        }
    }

    /* ── Normal resolution from root FS ──────────────── */
    char *p = clean_path;
    if (*p == '/') p++;

    fs_node_t *current = fs_root;
    char *token = p;
    
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (strlen(token) > 0) {
                fs_node_t *next = finddir_fs(current, token);
                // Free intermediate transient node if we're moving deeper
                if (current != fs_root && (current->flags & FS_TRANSIENT)) {
                    kfree(current);
                }
                current = next;
                if (!current) return 0;
            }
            token = p + 1;
        }
        p++;
    }

    // Final component
    if (strlen(token) > 0) {
        fs_node_t *next = finddir_fs(current, token);
        // Free intermediate transient node
        if (current != fs_root && (current->flags & FS_TRANSIENT)) {
            kfree(current);
        }
        current = next;
    }

    return current;
}

int open(const char *name, int flags) {
    // Use the new path resolver
    fs_node_t *node = vfs_resolve_path(name);
    
    if (!node && (flags & O_CREAT)) {
        char parent_path[512];
        char basename[128];
        vfs_get_parent_path(parent_path, name);
        vfs_get_basename(basename, name);
        
        // First try: ask the real FS to create the file
        fs_node_t *parent = vfs_resolve_path(parent_path);
        if (parent && parent->create) {
            create_fs(parent, basename, 0);
            node = vfs_resolve_path(name); // Re-resolve to get new node
        }
        // Free the ephemeral parent node (TRANSIENT from finddir)
        if (parent && (parent->flags & FS_TRANSIENT)) kfree(parent);

        // Fallback: the FS has no create (e.g. initrd) — mint a transient
        // in-memory node.  close() already knows how to free FS_TRANSIENT.
        if (!node) {
            fs_node_t *mock = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
            memory_set((uint8_t*)mock, 0, sizeof(fs_node_t));
            strcpy(mock->name, basename);
            mock->flags  = FS_FILE | FS_TRANSIENT | FS_IMPL_HEAP; // impl is a heap buffer
            mock->length = 0;
            mock->read   = mock_read;
            mock->write  = mock_write;
            node = mock;
        }
    }

    if (!node) return -1;

    // Truncate: zero the backing buffer and reset file length
    if ((flags & O_TRUNC) && (flags & O_WRONLY || flags & O_RDWR)) {
        if (node->impl) {
            memory_set(node->impl, 0, 1024);
        }
        node->length = 0;
    }

    for (int i = FIRST_USER_FD; i < MAX_FD; i++) {
        if (!current_task->fd_table[i]) {
            current_task->fd_table[i] = file_create(node, flags);
            return i;
        }
    }
    return -1;
}

int close(int fd) {
    if (fd < 0 || fd >= MAX_FD || !current_task->fd_table[fd]) return -1;
    
    file_t *f = current_task->fd_table[fd];
    current_task->fd_table[fd] = 0; // Remove from table first
    
    f->refcount--;
    
    // Pipe specific refcounting
    if (f->node->flags & FS_PIPE) {
        pipe_t *p = (pipe_t*)f->node->impl;
        if (f->flags & O_WRONLY) pipe_remove_writer(p);
        else pipe_remove_reader(p);
    }

    if (f->refcount == 0) { 
        // Capability check: Only close and free non-persistent nodes
        if (!(f->node->flags & FS_PERSISTENT)) {
            // Special handling for pipes
            if (f->node->flags & FS_PIPE) {
                pipe_t *p = (pipe_t*)f->node->impl;
                if (p->readers == 0 && p->writers == 0) {
                    if (p->buffer) kfree(p->buffer);
                    kfree(p);
                    kfree(f->node);
                }
            } else {
                close_fs(f->node);
                // Only free impl if the node owns a heap-allocated impl buffer
                if (f->node->flags & FS_TRANSIENT) {
                    if ((f->node->flags & FS_IMPL_HEAP) && f->node->impl) kfree(f->node->impl);
                    kfree(f->node);
                }
            }
        }
        kfree(f);
    }
    return 0;
}

void vfs_close_all_fds(void *task_ptr) {
    task_t *task = (task_t*)task_ptr;
    for (int i = 0; i < MAX_FD; i++) {
        if (task->fd_table[i]) {
            file_t *f = task->fd_table[i];
            task->fd_table[i] = 0;
            
            f->refcount--;
            
            // Pipe specific refcounting
            if (f->node->flags & FS_PIPE) {
                pipe_t *p = (pipe_t*)f->node->impl;
                if (f->flags & O_WRONLY) pipe_remove_writer(p);
                else pipe_remove_reader(p);
            }

            if (f->refcount == 0) { 
                if (!(f->node->flags & FS_PERSISTENT)) {
                    if (f->node->flags & FS_PIPE) {
                        pipe_t *p = (pipe_t*)f->node->impl;
                        if (p->readers == 0 && p->writers == 0) {
                            if (p->buffer) kfree(p->buffer);
                            kfree(p);
                            kfree(f->node);
                        }
                    } else {
                        close_fs(f->node);
                        if (f->node->flags & FS_TRANSIENT) {
                            if ((f->node->flags & FS_IMPL_HEAP) && f->node->impl) kfree(f->node->impl);
                            kfree(f->node);
                        }
                    }
                }
                kfree(f);
            }
        }
    }
}

int read(int fd, char *buf, int size) {
    if (fd < 0 || fd >= MAX_FD || !current_task->fd_table[fd]) return -1;
    file_t *f = current_task->fd_table[fd];
    
    // Permission check
    if (f->flags == O_WRONLY) return -1;

    uint64_t res = read_fs(f->node, f->offset, size, (uint8_t*)buf);
    f->offset += res;
    return (int)res;
}

int write(int fd, const char *buf, int size) {
    if (fd < 0 || fd >= MAX_FD || !current_task->fd_table[fd]) return -1;
    file_t *f = current_task->fd_table[fd];

    // Permission check
    if (f->flags == O_RDONLY) return -1;

    uint64_t res = write_fs(f->node, f->offset, size, (uint8_t*)buf);
    if (res == 0 && size > 0 && (f->flags & O_CREAT)) {
        res = size; // Mock success for O_CREAT files
    }
    f->offset += res;
    return (int)res;
}

int seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FD || !current_task->fd_table[fd]) return -1;
    file_t *f = current_task->fd_table[fd];
    
    uint64_t new_offset = f->offset;
    
    switch (whence) {
        case KABI_SEEK_SET:
            new_offset = offset;
            break;
        case KABI_SEEK_CUR:
            new_offset += offset;
            break;
        case KABI_SEEK_END:
            new_offset = f->node->length + offset;
            break;
        default:
            return -1;
    }
    
    // Bounds check: don't allow seeking before start
    if ((int64_t)new_offset < 0) return -1;

    f->offset = new_offset;
    return (int)f->offset;
}

// add dup syscall

int dup(int oldfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -1;

    file_t *f = current_task->fd_table[oldfd];
    if (!f) return -1;

    // let's allocate the lowest available fd >= FIRST_USER_FD (3)
    // so whichever unallocated in greater than 2
    // point to same file_t
    for (int newfd = FIRST_USER_FD; newfd < MAX_FD; newfd++) {
        if (!current_task->fd_table[newfd]) {
            current_task->fd_table[newfd] = f;
            f->refcount++;

            if (f->node->flags & FS_PIPE) {
                pipe_t *p = (pipe_t*)f->node->impl;
                if (f->flags & O_WRONLY) pipe_add_writer(p);
                else pipe_add_reader(p);
            }

            return newfd;
        }
    }
    return -1; // no free fd
}

// add dup2 syscall with newfd and oldfd
// make newfd refer to the same openfile as oldfd
int dup2(int oldfd, int newfd) {
    // check valid value for oldfd and newfd
    if (oldfd < 0 || oldfd >= MAX_FD) return -1;
    if (newfd < 0 || newfd >= MAX_FD) return -1;

    
    file_t *f = current_task->fd_table[oldfd];
    if (!f) return -1;

    if (oldfd == newfd) // same old and new fd. edge case
        return newfd;

    if (current_task->fd_table[newfd]) {
        close(newfd);  // triggers refcount + teardown if needed
    }

    current_task->fd_table[newfd] = f;
    f->refcount++;

    if (f->node->flags & FS_PIPE) {
        pipe_t *p = (pipe_t*)f->node->impl;
        if (f->flags & O_WRONLY) pipe_add_writer(p);
        else pipe_add_reader(p);
    }

    return newfd;
}

int sys_mkdir(const char *path) {
    if (!path) return -1;
    char parent_path[512];
    char basename[128];
    vfs_get_parent_path(parent_path, path);
    vfs_get_basename(basename, path);
    fs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || !parent->mkdir) {
        if (parent && (parent->flags & FS_TRANSIENT)) kfree(parent);
        return -1;
    }
    parent->mkdir(parent, basename, 0);
    if (parent->flags & FS_TRANSIENT) kfree(parent);
    return 0;
}

int sys_unlink(const char *path) {
    if (!path) return -1;
    char parent_path[512];
    char basename[128];
    vfs_get_parent_path(parent_path, path);
    vfs_get_basename(basename, path);
    fs_node_t *parent = vfs_resolve_path(parent_path);
    if (!parent || !parent->unlink) {
        if (parent && (parent->flags & FS_TRANSIENT)) kfree(parent);
        return -1;
    }
    int r = parent->unlink(parent, basename);
    if (parent->flags & FS_TRANSIENT) kfree(parent);
    return r;
}

// --- K-ABI Implementations ---
kabi_fs_node_t* kabi_vfs_get_root() {
    return (kabi_fs_node_t*)fs_root;
}

kabi_fs_node_t* kabi_vfs_resolve_path(const char *path) {
    return (kabi_fs_node_t*)vfs_resolve_path(path);
}

uint64_t kabi_vfs_read(kabi_fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    return read_fs((fs_node_t*)node, offset, size, buffer);
}

uint64_t kabi_vfs_write(kabi_fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    return write_fs((fs_node_t*)node, offset, size, buffer);
}

kabi_dirent_t* kabi_vfs_readdir(kabi_fs_node_t *node, uint64_t index) {
    return (kabi_dirent_t*)readdir_fs((fs_node_t*)node, index);
}

kabi_fs_node_t* kabi_vfs_finddir(kabi_fs_node_t *node, char *name) {
    return (kabi_fs_node_t*)finddir_fs((fs_node_t*)node, name);
}

/* Mount table accessors (used by sys_readdir to inject mount entries) */
int vfs_get_mount_count(void) { return num_mounts; }
const char *vfs_get_mount_path(int idx) {
    if (idx < 0 || idx >= num_mounts || !mount_table[idx].active) return 0;
    return mount_table[idx].path;
}
