#include "fat32_vfs.h"
#include "fat32_bpb.h"
#include "fat32_fat.h"
#include "fat32_dir.h"
#include "fat32_file.h"
#include "../../core/vfs_core.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../modules/partition/mbr.h"
#include "../../core/block_dev.h"

/*
 * Per-mount FAT32 context.
 * Each fat32_vfs_mount() allocates a slot here. Every fs_node_t created
 * for that mount stores the slot index in node->mask so that VFS ops
 * can retrieve the correct device + BPB without global state.
 */
#define FAT32_MAX_MOUNTS 4
typedef struct {
    uint32_t       dev;
    fat32_mount_t  info;
    int            active;
} fat32_ctx_t;

static fat32_ctx_t fat32_mounts[FAT32_MAX_MOUNTS];
static int fat32_num_mounts = 0;

/* Helper: look up context from a node */
static fat32_ctx_t *ctx_of(fs_node_t *node) {
    int idx = (int)node->mask;
    if (idx < 0 || idx >= fat32_num_mounts) idx = 0;
    return &fat32_mounts[idx];
}

static void fat32_vfs_create(fs_node_t *node, char *name, uint16_t mask);

/**
 * VFS Adapter: readdir
 */
static kabi_dirent_t* fat32_vfs_readdir(fs_node_t *node, uint32_t index) {
    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    uint32_t cluster = node->inode;
    if (cluster == 0) cluster = m->root_cluster;

    uint32_t current_index = 0;
    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS) return 0;

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr == FAT_ATTR_LFN) continue;

                if (current_index == index) {
                    kabi_dirent_t *dirent = (kabi_dirent_t*)kmalloc(sizeof(kabi_dirent_t), 0, 0);
                    int p = 0;
                    for (int n = 0; n < 8 && entries[i].name[n] != ' '; n++) dirent->name[p++] = entries[i].name[n];
                    if (entries[i].name[8] != ' ') {
                        dirent->name[p++] = '.';
                        for (int n = 8; n < 11 && entries[i].name[n] != ' '; n++) dirent->name[p++] = entries[i].name[n];
                    }
                    dirent->name[p] = '\0';
                    dirent->ino = (entries[i].cluster_hi << 16) | entries[i].cluster_lo;
                    dirent->size = entries[i].size;
                    dirent->attr = entries[i].attr;
                    dirent->type = (entries[i].attr & FAT_ATTR_DIR) ? FS_DIRECTORY : FS_FILE;
                    return dirent;
                }
                current_index++;
            }
        }
        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return 0;
}

/**
 * VFS Adapter: finddir
 */
static fs_node_t* fat32_vfs_finddir(fs_node_t *node, char *name) {
    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    uint32_t cluster = node->inode;
    if (cluster == 0) cluster = m->root_cluster;

    uint8_t target_83[11];
    memory_set(target_83, ' ', 11);
    int p = 0, j = 0;
    while (name[p] != '.' && name[p] != '\0' && j < 8) {
        char c = name[p++];
        if (c >= 'a' && c <= 'z') c -= 32;
        target_83[j++] = c;
    }
    while (name[p] != '.' && name[p] != '\0') p++;
    if (name[p] == '.') {
        p++; j = 8;
        while (name[p] != '\0' && j < 11) {
            char c = name[p++];
            if (c >= 'a' && c <= 'z') c -= 32;
            target_83[j++] = c;
        }
    }

    while (cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);

        for (uint32_t s = 0; s < m->sectors_per_cluster; s++) {
            uint8_t buf[512];
            if (kabi_block_read(dev, lba + s, buf) != KABI_SUCCESS) return 0;

            fat32_dirent_t *entries = (fat32_dirent_t *)buf;
            for (int i = 0; i < 16; i++) {
                if (entries[i].name[0] == 0x00) return 0;
                if (entries[i].name[0] == 0xE5) continue;
                if (entries[i].attr == FAT_ATTR_LFN) continue;

                int match = 1;
                for (int n = 0; n < 11; n++) {
                    if (entries[i].name[n] != target_83[n]) { match = 0; break; }
                }

                if (match) {
                    fs_node_t *child = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
                    memory_set((uint8_t*)child, 0, sizeof(fs_node_t));
                    strcpy(child->name, name);
                    child->length = entries[i].size;
                    child->inode = (entries[i].cluster_hi << 16) | entries[i].cluster_lo;
                    child->impl = (void*)node->inode; // parent cluster
                    child->mask = node->mask;  /* inherit mount index */
                    child->flags = (entries[i].attr & FAT_ATTR_DIR) ? FS_DIRECTORY : FS_FILE;
                    child->flags |= FS_TRANSIENT;
                    
                    child->read    = node->read;
                    child->write   = node->write;
                    child->readdir = node->readdir;
                    child->finddir = node->finddir;
                    child->create  = node->create;
                    child->mkdir   = node->mkdir;
                    child->unlink  = node->unlink;
                    return child;
                }
            }
        }
        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return 0;
}

/**
 * VFS Adapter: read
 */
static uint32_t fat32_vfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (offset >= node->length) return 0;
    if (offset + size > node->length) size = node->length - offset;

    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    uint32_t cluster = node->inode;
    uint32_t bytes_per_cluster = m->sectors_per_cluster * 512;
    
    uint32_t clusters_to_skip = offset / bytes_per_cluster;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        cluster = fat32_get_next_cluster(dev, m, cluster);
        if (cluster >= 0x0FFFFFF8 || cluster < 2) return 0;
    }
    offset %= bytes_per_cluster;

    uint32_t bytes_read = 0;
    while (bytes_read < size && cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);
        if (kabi_debug_enabled()) {
            kprint("[FAT32] reading cluster: 0x"); char s[16]; hex_to_ascii(cluster, s); kprint(s);
            kprint(" LBA: 0x"); hex_to_ascii(lba, s); kprint(s); kprint("\n");
        }
        
        uint32_t sector_offset = offset / 512;
        uint32_t byte_in_sector = offset % 512;

        for (uint32_t s = sector_offset; s < m->sectors_per_cluster && bytes_read < size; s++) {
            uint8_t sector_buf[512];
            if (kabi_block_read(dev, lba + s, sector_buf) != KABI_SUCCESS) return bytes_read;

            uint32_t to_copy = 512 - byte_in_sector;
            if (to_copy > (size - bytes_read)) to_copy = size - bytes_read;

            memory_copy(sector_buf + byte_in_sector, buffer + bytes_read, to_copy);
            
            bytes_read += to_copy;
            byte_in_sector = 0;
            offset = 0; 
        }

        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return bytes_read;
}

static int fat32_vfs_expand(fs_node_t *node, uint32_t new_size) {
    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    uint32_t bytes_per_cluster = m->sectors_per_cluster * 512;
    
    uint32_t current_clusters = (node->length == 0) ? 0 : (node->length + bytes_per_cluster - 1) / bytes_per_cluster;
    uint32_t needed_clusters = (new_size == 0) ? 0 : (new_size + bytes_per_cluster - 1) / bytes_per_cluster;

    if (needed_clusters > current_clusters) {
        uint32_t last_cluster = 0;
        
        if (node->inode == 0) {
            uint32_t start = fat32_find_free_cluster(dev, m);
            if (start == 0) return KABI_ENOMEM;
            
            fat32_set_cluster(dev, m, start, 0x0FFFFFFF);
            node->inode = start;
            last_cluster = start;
            current_clusters = 1;
            
            fat32_update_dirent(dev, m, (uint32_t)node->impl, node->name, 0xFFFFFFFF, start);
            
            uint32_t start_lba = fat32_cluster_to_lba(m, start);
            uint8_t zero[512]; memory_set(zero, 0, 512);
            for (uint32_t s = 0; s < m->sectors_per_cluster; s++) kabi_block_write(dev, start_lba + s, zero);
        } else {
            uint32_t cluster = node->inode;
            for (uint32_t i = 1; i < current_clusters; i++) {
                uint32_t next = fat32_get_next_cluster(dev, m, cluster);
                if (next >= 0x0FFFFFF8) break;
                cluster = next;
            }
            last_cluster = cluster;
        }

        for (uint32_t i = current_clusters; i < needed_clusters; i++) {
            uint32_t next = fat32_find_free_cluster(dev, m);
            if (next == 0) return KABI_ENOMEM;
            
            fat32_set_cluster(dev, m, next, 0x0FFFFFFF);
            fat32_set_cluster(dev, m, last_cluster, next);
            
            uint32_t next_lba = fat32_cluster_to_lba(m, next);
            uint8_t zero[512]; memory_set(zero, 0, 512);
            for (uint32_t s = 0; s < m->sectors_per_cluster; s++) kabi_block_write(dev, next_lba + s, zero);
            
            last_cluster = next;
        }
    }

    node->length = new_size;
    return fat32_update_dirent(dev, m, (uint32_t)node->impl, node->name, new_size, 0xFFFFFFFF);
}

/**
 * VFS Adapter: write
 */
static uint32_t fat32_vfs_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (offset + size > node->length) {
        if (fat32_vfs_expand(node, offset + size) != KABI_SUCCESS) return 0;
    }

    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    uint32_t cluster = node->inode;
    uint32_t bytes_per_cluster = m->sectors_per_cluster * 512;
    
    uint32_t clusters_to_skip = offset / bytes_per_cluster;
    for (uint32_t i = 0; i < clusters_to_skip; i++) {
        cluster = fat32_get_next_cluster(dev, m, cluster);
        if (cluster >= 0x0FFFFFF8 || cluster < 2) return 0;
    }
    offset %= bytes_per_cluster;

    uint32_t bytes_written = 0;
    while (bytes_written < size && cluster < 0x0FFFFFF8) {
        uint32_t lba = fat32_cluster_to_lba(m, cluster);
        
        uint32_t sector_offset = offset / 512;
        uint32_t byte_in_sector = offset % 512;

        for (uint32_t s = sector_offset; s < m->sectors_per_cluster && bytes_written < size; s++) {
            uint8_t sector_buf[512];
            
            if (byte_in_sector != 0 || (size - bytes_written) < 512) {
                if (kabi_block_read(dev, lba + s, sector_buf) != KABI_SUCCESS) return bytes_written;
            }

            uint32_t to_copy = 512 - byte_in_sector;
            if (to_copy > (size - bytes_written)) to_copy = size - bytes_written;

            memory_copy(buffer + bytes_written, sector_buf + byte_in_sector, to_copy);
            
            if (kabi_block_write(dev, lba + s, sector_buf) != KABI_SUCCESS) return bytes_written;
            
            bytes_written += to_copy;
            byte_in_sector = 0;
            offset = 0; 
        }

        cluster = fat32_get_next_cluster(dev, m, cluster);
    }

    return bytes_written;
}

/**
 * VFS Adapter: create
 */
static void fat32_vfs_create(fs_node_t *node, char *name, uint16_t mask) {
    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    
    fat32_dirent_t existing;
    if (fat32_find_file(dev, m, name, node->inode, &existing) == KABI_SUCCESS) {
        return;
    }

    if (kabi_debug_enabled()) {
        kprint("[FAT32] Creating file: "); kprint(name); kprint("\n");
    }
    
    if (fat32_add_dirent(dev, m, node->inode, name, 0, 0, FAT_ATTR_ARCHIVE) != KABI_SUCCESS) {
        kprint("[FAT32] Failed to add dirent\n");
    }
}

/**
 * VFS Adapter: mkdir
 */
static void fat32_vfs_mkdir(fs_node_t *node, char *name, uint16_t mask) {
    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    if (kabi_debug_enabled()) {
        kprint("[FAT32] mkdir: "); kprint(name); kprint("\n");
    }
    int r = fat32_mkdir(dev, m, node->inode, name);
    if (r != KABI_SUCCESS) {
        kprint("[FAT32] mkdir failed\n");
    }
}

/**
 * VFS Adapter: unlink
 */
static int fat32_vfs_unlink(fs_node_t *node, char *name) {
    fat32_ctx_t *ctx = ctx_of(node);
    fat32_mount_t *m = &ctx->info;
    uint32_t dev = ctx->dev;
    if (kabi_debug_enabled()) {
        kprint("[FAT32] unlink: "); kprint(name); kprint("\n");
    }
    return fat32_unlink(dev, m, node->inode, name);
}

int fat32_vfs_mount(uint32_t dev, const char *mountpoint) {
    if (fat32_num_mounts >= FAT32_MAX_MOUNTS) {
        kprint("[FAT32] Max mounts reached\n");
        return KABI_ENOMEM;
    }

    kabi_partition_t part;
    kabi_block_device_t *blk_dev = block_dev_get_by_index(dev);
    
    // If this is already a partition, mount it directly.
    // Use the name to check if it ends in "pN" or use the is_partition flag.
    if (blk_dev && blk_dev->is_partition) {
        part.found = 1;
        part.start_lba = 0; // Relative to the virtual device
    } else {
        // Find FAT32 partition within the device (MBR or Superfloppy fallback)
        if (mbr_find_fat32(dev, &part) != KABI_SUCCESS || !part.found) {
            return KABI_ENOENT;
        }
    }

    int slot = fat32_num_mounts;
    fat32_ctx_t *ctx = &fat32_mounts[slot];

    // Read BPB using the partition's start LBA
    if (fat32_read_bpb(dev, part.start_lba, &ctx->info) != KABI_SUCCESS) {
        return KABI_EIO;
    }

    ctx->dev = dev;
    ctx->active = 1;
    fat32_num_mounts++;

    /* Create ops struct — one per mount so the function pointers are the same
     * but each mount's root node carries the correct slot index in node->mask. */
    static kabi_fs_ops_t fat_ops;
    fat_ops.read = (void*)fat32_vfs_read;
    fat_ops.write = (void*)fat32_vfs_write;
    fat_ops.readdir = (void*)fat32_vfs_readdir;
    fat_ops.finddir = (void*)fat32_vfs_finddir;
    fat_ops.create = (void*)fat32_vfs_create;
    fat_ops.mkdir = (void*)fat32_vfs_mkdir;
    fat_ops.unlink = (void*)fat32_vfs_unlink;

    kabi_vfs_register(&fat_ops, mountpoint);

    /* Tag the newly registered root node with our mount slot index.
     * kabi_vfs_register creates the node and for "/" it sets fs_root,
     * for sub-mounts it adds to the mount table. We need to find it
     * and set its mask. We do this by resolving the path. */
    extern fs_node_t *fs_root;
    if (strcmp((char*)mountpoint, "/") == 0) {
        fs_root->mask = slot;
    } else {
        /* The mount table root node was just created — resolve it */
        fs_node_t *mnode = vfs_resolve_path(mountpoint);
        if (mnode) {
            mnode->mask = slot;
        }
    }

    return KABI_SUCCESS;
}

/* --- Module Registration --- */
kabi_module_t __kabi_module_fat32 = {
    .name           = "fs_fat32",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = NULL, // Initialized via fat32_vfs_mount() at boot
    .exit           = NULL,
    .description    = "FAT32 filesystem driver"
};
