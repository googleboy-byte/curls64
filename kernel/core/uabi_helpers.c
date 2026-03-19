#include "uabi_helpers.h"
#include "task.h"
#include "vfs_core.h"
#include "../../libc/string.h"
#include "../../libc/mem.h"
#include "../../include/uabi/uabi_v1.h"
#include "../../include/kabi/kabi_v1.h"
#include "block_dev.h"

// Define v2 struct equivalents locally to avoid conflict with uabi_v1.h typedefs
typedef struct {
    char name[128];
    uint64_t inode;
    uint64_t size;
    uint8_t type;
    uint8_t attr;
} uabi_dirent_v2_t;

typedef struct {
    uint64_t size;
    uint64_t inode;
    uint8_t type;
} uabi_stat_v2_t;

typedef struct {
    char name[32];
    uint64_t sectors;
    uint32_t sector_size;
    int is_partition;
    int parent_dev;
} uabi_devinfo_v2_t;

typedef struct {
    int pid;
    int parent_pid;
    int state;
    uint64_t user_rip;
    uint64_t user_rsp;
    uint64_t ticks;
} uabi_proc_info_v2_t;

typedef struct {
    uint64_t total_frames;
    uint64_t used_frames;
    uint64_t free_frames;
} uabi_memstat_v2_t;

// sys_readdir: Read directory entries
int sys_readdir(const char *path, void *entries_buf, int max_entries, int is64) {
    if (!path || !entries_buf || max_entries <= 0) return -1;
    
    fs_node_t *node = vfs_resolve_path(path);
    if (!node) return -1;
    
    uint32_t stride = is64 ? sizeof(uabi_dirent_v2_t) : sizeof(uabi_dirent_t);
    uint8_t *user_base = (uint8_t *)entries_buf;

    int count = 0;
    int i = 0;
    struct dirent *ent;
    
    while (count < max_entries && (ent = readdir_fs(node, i++)) != 0) {
        uint8_t *dst = user_base + (count * stride);
        if (is64) {
            uabi_dirent_v2_t entry;
            memory_set((uint8_t*)&entry, 0, sizeof(entry));
            strcpy(entry.name, ent->name);
            entry.inode = ent->ino;
            entry.size = ent->size;
            entry.type = ent->type;
            entry.attr = ent->attr;
            memory_copy((uint8_t*)&entry, dst, sizeof(entry));
        } else {
            uabi_dirent_t entry;
            memory_set((uint8_t*)&entry, 0, sizeof(entry));
            strcpy(entry.name, ent->name);
            entry.inode = ent->ino;
            entry.size = ent->size;
            entry.type = ent->type;
            entry.attr = ent->attr;
            memory_copy((uint8_t*)&entry, dst, sizeof(entry));
        }
        kfree(ent);
        count++;
    }

    /* Inject mount-point entries when listing root */
    char clean[512];
    vfs_canonicalize_path(clean, path);
    if (strcmp(clean, "/") == 0) {
        extern int vfs_get_mount_count(void);
        extern const char *vfs_get_mount_path(int idx);
        int nm = vfs_get_mount_count();
        for (int m = 0; m < nm && count < max_entries; m++) {
            const char *mp = vfs_get_mount_path(m);
            if (!mp) continue;
            const char *name = mp + 1;
            if (*name == '\0') continue;

            int dup = 0;
            for (int d = 0; d < count; d++) {
                const char *a;
                if (is64) {
                    uabi_dirent_v2_t *d_ent = (uabi_dirent_v2_t*)(user_base + (d * stride));
                    a = d_ent->name;
                } else {
                    uabi_dirent_t *d_ent = (uabi_dirent_t*)(user_base + (d * stride));
                    a = d_ent->name;
                }
                const char *b = name;
                int match = 1;
                while (*a && *b) {
                    char ca = *a, cb = *b;
                    if (ca >= 'a' && ca <= 'z') ca -= 32;
                    if (cb >= 'a' && cb <= 'z') cb -= 32;
                    if (ca != cb) { match = 0; break; }
                    a++; b++;
                }
                if (match && *a == '\0' && *b == '\0') { dup = 1; break; }
            }
            if (dup) continue;

            uint8_t *dst = user_base + (count * stride);
            if (is64) {
                uabi_dirent_v2_t entry;
                memory_set((uint8_t*)&entry, 0, sizeof(entry));
                int k = 0;
                while (name[k] && k < 127) {
                    char c = name[k];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    entry.name[k++] = c;
                }
                entry.name[k] = '\0';
                entry.inode = 900 + m;
                entry.size = 0;
                entry.type = 2; // FS_DIRECTORY
                entry.attr = 0;
                memory_copy((uint8_t*)&entry, dst, sizeof(entry));
            } else {
                uabi_dirent_t entry;
                memory_set((uint8_t*)&entry, 0, sizeof(entry));
                int k = 0;
                while (name[k] && k < 127) {
                    char c = name[k];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    entry.name[k++] = c;
                }
                entry.name[k] = '\0';
                entry.inode = 900 + m;
                entry.size = 0;
                entry.type = 2;
                entry.attr = 0;
                memory_copy((uint8_t*)&entry, dst, sizeof(entry));
            }
            count++;
        }
    }

    if (node->flags & FS_TRANSIENT) kfree(node);

    return count;
}

// sys_getcwd: Get current working directory
int sys_getcwd(char *buf, uint32_t size) {
    if (!buf || size == 0 || !current_task) return -1;
    
    uint32_t len = strlen((char*)current_task->cwd);
    if (len >= size) return -1;
    
    strcpy(buf, (char*)current_task->cwd);
    return 0;
}

// sys_chdir: Change current working directory
int sys_chdir(const char *path) {
    if (!path || !current_task) return -1;
    
    char full_path[512];
    if (path[0] == '/') {
        strcpy(full_path, path);
    } else {
        strcpy(full_path, (char*)current_task->cwd);
        if (strcmp((char*)current_task->cwd, "/") != 0) {
            strcat(full_path, "/");
        }
        strcat(full_path, path);
    }
    
    fs_node_t *node = vfs_resolve_path(full_path);
    if (!node) return -1;
    
    vfs_canonicalize_path((char*)current_task->cwd, full_path);
    
    if (node->flags & FS_TRANSIENT) kfree(node);
    
    return 0;
}

// sys_stat: Get file statistics
int sys_stat(const char *path, void *stat_buf, int is64) {
    if (!path || !stat_buf) return -1;
    
    fs_node_t *node = vfs_resolve_path(path);
    if (!node) return -1;
    
    if (is64) {
        uabi_stat_v2_t *stat = (uabi_stat_v2_t *)stat_buf;
        stat->size = node->length;
        stat->inode = node->inode;
        stat->type = (node->flags & FS_DIRECTORY) ? 2 : 1;
    } else {
        uabi_stat_t *stat = (uabi_stat_t *)stat_buf;
        stat->size = node->length;
        stat->inode = node->inode;
        stat->type = (node->flags & FS_DIRECTORY) ? 2 : 1;
    }
    
    if (node->flags & FS_TRANSIENT) kfree(node);
    
    return 0;
}

// sys_ps: Get process list
int sys_ps(void *procs_buf, int max_procs, int is64) {
    if (!procs_buf || max_procs <= 0) return -1;
    
    uint32_t stride = is64 ? sizeof(uabi_proc_info_v2_t) : sizeof(uabi_proc_info_t);
    uint8_t *user_base = (uint8_t *)procs_buf;
    int count = 0;
    
    kabi_task_iter_t it;
    kabi_task_info_t info;
    
    if (kabi_task_iter_begin(&it) != KABI_SUCCESS) {
        return 0;
    }
    
    while (count < max_procs && kabi_task_next(&it, &info)) {
        uint8_t *dst = user_base + (count * stride);
        if (is64) {
            uabi_proc_info_v2_t p;
            memory_set((uint8_t*)&p, 0, sizeof(p));
            p.pid = info.id;
            p.parent_pid = info.parent_id;
            p.state = info.state;
            p.user_rip = info.user_eip;
            p.user_rsp = info.user_esp;
            p.ticks = info.ticks;
            memory_copy((uint8_t*)&p, dst, sizeof(p));
        } else {
            uabi_proc_info_t p;
            memory_set((uint8_t*)&p, 0, sizeof(p));
            p.pid = info.id;
            p.parent_pid = info.parent_id;
            p.state = info.state;
            p.user_eip = info.user_eip;
            p.user_esp = info.user_esp;
            p.ticks = info.ticks;
            memory_copy((uint8_t*)&p, dst, sizeof(p));
        }
        count++;
    }
    
    return count;
}

// sys_memstat: Get memory statistics
int sys_memstat(void *stat_buf, int is64) {
    if (!stat_buf) return -1;
    
    kabi_pmm_stats_t pmm;
    kabi_get_pmm_stats(&pmm);
    
    if (is64) {
        uabi_memstat_v2_t *stat = (uabi_memstat_v2_t *)stat_buf;
        stat->total_frames = pmm.total_frames;
        stat->used_frames = pmm.used_frames;
        stat->free_frames = pmm.free_frames;
    } else {
        uabi_memstat_t *stat = (uabi_memstat_t *)stat_buf;
        stat->total_frames = pmm.total_frames;
        stat->used_frames = pmm.used_frames;
        stat->free_frames = pmm.free_frames;
    }
    
    return 0;
}

// sys_devinfo: Get block device info
int sys_devinfo(int index, void *info_buf, int is64) {
    if (!info_buf) return -1;
    
    extern kabi_block_device_t* block_dev_get_by_index(int index);
    kabi_block_device_t *dev = block_dev_get_by_index(index);
    if (!dev) return -2;

    if (is64) {
        uabi_devinfo_v2_t *uinfo = (uabi_devinfo_v2_t *)info_buf;
        for (int i = 0; i < 31 && dev->name[i]; i++) {
            uinfo->name[i] = dev->name[i];
            uinfo->name[i+1] = '\0';
        }
        uinfo->sectors = dev->size;
        uinfo->sector_size = 512;
        uinfo->is_partition = dev->is_partition;
        uinfo->parent_dev = (int)dev->parent_dev;
    } else {
        uabi_devinfo_t *uinfo = (uabi_devinfo_t *)info_buf;
        for (int i = 0; i < 31 && dev->name[i]; i++) {
            uinfo->name[i] = dev->name[i];
            uinfo->name[i+1] = '\0';
        }
        uinfo->sectors = dev->size;
        uinfo->sector_size = 512;
        uinfo->is_partition = dev->is_partition;
        uinfo->parent_dev = (int)dev->parent_dev;
    }
    
    return 0;
}

// sys_getchar: Get a single character without echo
int sys_getchar(void) {
    extern void get_char_noecho(char *c);
    char c;
    get_char_noecho(&c);
    return (int)(unsigned char)c;
}
