#include "uabi_helpers.h"
#include "task.h"
#include "vfs_core.h"
#include "../../libc/string.h"
#include "../../libc/mem.h"
#include "../../include/uabi/uabi_v1.h"
#include "../../include/kabi/kabi_v1.h"



// sys_readdir: Read directory entries
int sys_readdir(const char *path, void *entries_buf, int max_entries) {
    if (!path || !entries_buf || max_entries <= 0) return -1;
    
    fs_node_t *node = (fs_node_t*)kabi_vfs_resolve_path(path);
    if (!node) return -1;
    
    // Use kernel buffer to avoid user-space access issues
    uabi_dirent_t kernel_entry;
    uabi_dirent_t *user_entries = (uabi_dirent_t *)entries_buf;
    int count = 0;
    int i = 0;
    kabi_dirent_t *ent;
    
    while (count < max_entries && (ent = kabi_vfs_readdir((kabi_fs_node_t*)node, i++)) != 0) {
        // Copy to kernel buffer first
        strcpy(kernel_entry.name, ent->name);
        kernel_entry.inode = ent->ino;
        kernel_entry.size = ent->size;
        kernel_entry.type = ent->type;
        kernel_entry.attr = ent->attr;
        
        // Copy to user space byte-by-byte
        uint8_t *src = (uint8_t *)&kernel_entry;
        uint8_t *dst = (uint8_t *)&user_entries[count];
        for (uint32_t j = 0; j < sizeof(uabi_dirent_t); j++) {
            dst[j] = src[j];
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
            /* Extract directory name from mount path (e.g. "/usb" → "USB") */
            const char *name = mp + 1; /* skip leading '/' */
            if (*name == '\0') continue; /* skip root mounts */

            /* Check if this name already exists from the FS entries (avoid dup) */
            int dup = 0;
            for (int d = 0; d < count; d++) {
                /* Case-insensitive compare (FAT32 returns uppercase) */
                const char *a = user_entries[d].name;
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

            memory_set((uint8_t *)&kernel_entry, 0, sizeof(kernel_entry));
            /* Convert name to uppercase for consistency */
            int k = 0;
            while (name[k] && k < 127) {
                char c = name[k];
                if (c >= 'a' && c <= 'z') c -= 32;
                kernel_entry.name[k] = c;
                k++;
            }
            kernel_entry.name[k] = '\0';
            kernel_entry.inode = 900 + m;
            kernel_entry.size = 0;
            kernel_entry.type = 2; /* FS_DIRECTORY */
            kernel_entry.attr = 0;

            uint8_t *src = (uint8_t *)&kernel_entry;
            uint8_t *dst = (uint8_t *)&user_entries[count];
            for (uint32_t j = 0; j < sizeof(uabi_dirent_t); j++) {
                dst[j] = src[j];
            }
            count++;
        }
    }

    // Free the ephemeral node allocated by fat32_vfs_finddir (marked FS_TRANSIENT)
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
    
    // Resolve the path to verify it exists
    fs_node_t *node = (fs_node_t*)kabi_vfs_resolve_path(full_path);
    if (!node) return -1;
    
    // Normalize and store the result as the new current working directory
    vfs_canonicalize_path((char*)current_task->cwd, full_path);
    
    if (node->flags & FS_TRANSIENT) kfree(node);
    
    return 0;
}

// sys_stat: Get file statistics
int sys_stat(const char *path, void *stat_buf) {
    if (!path || !stat_buf) return -1;
    
    fs_node_t *node = (fs_node_t*)vfs_resolve_path(path);
    if (!node) return -1;
    
    uabi_stat_t *stat = (uabi_stat_t *)stat_buf;
    stat->size = node->length;
    stat->inode = node->inode;
    stat->type = (node->flags & FS_DIRECTORY) ? 2 : 1;
    
    if (node->flags & FS_TRANSIENT) kfree(node);
    
    return 0;
}

// sys_ps: Get process list
int sys_ps(void *procs_buf, int max_procs) {
    if (!procs_buf || max_procs <= 0) return -1;
    
    uabi_proc_info_t *procs = (uabi_proc_info_t *)procs_buf;
    int count = 0;
    
    kabi_task_iter_t it;
    kabi_task_info_t info;
    
    if (kabi_task_iter_begin(&it) != KABI_SUCCESS) {
        return 0;
    }
    
    while (count < max_procs && kabi_task_next(&it, &info)) {
        procs[count].pid = info.id;
        procs[count].parent_pid = info.parent_id;
        procs[count].state = info.state;
        procs[count].user_eip = info.user_eip;
        procs[count].user_esp = info.user_esp;
        procs[count].ticks = info.ticks;
        count++;
    }
    
    return count;
}

// sys_memstat: Get memory statistics
int sys_memstat(void *stat_buf) {
    if (!stat_buf) return -1;
    
    uabi_memstat_t *stat = (uabi_memstat_t *)stat_buf;
    kabi_pmm_stats_t pmm;
    
    kabi_get_pmm_stats(&pmm);
    
    stat->total_frames = pmm.total_frames;
    stat->used_frames = pmm.used_frames;
    stat->free_frames = pmm.free_frames;
    
    return 0;
}

// sys_getchar: Get a single character without echo
int sys_getchar(void) {
    extern void get_char_noecho(char *c);
    char c;
    get_char_noecho(&c);
    return (int)(unsigned char)c;
}
