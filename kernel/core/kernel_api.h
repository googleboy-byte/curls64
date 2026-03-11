#ifndef KERNEL_API_H
#define KERNEL_API_H

/**
 * Curls Kernel stable API
 * This header defines the "Sacred Layer" surface area available to modules.
 */

#include <stdint.h>
#include <stddef.h>

#include <kernel/arch_types.h>

/* --- Memory Management --- */
void *kmalloc(size_t size, int align, phys_addr_t *phys);
void kfree(void *p);

typedef struct {
    size_t   total_size;
    size_t   used_size;
    size_t   free_size;
    virt_addr_t max_addr;
} heap_stats_t;

void get_heap_stats(heap_stats_t *stats);

/* --- Output / Debugging --- */
void kprint(const char *c);
void kprint_at(const char *c, int col, int row);
void clear_screen();
void int_to_ascii(int n, char str[]);
void hex_to_ascii(uint64_t n, char str[]);

/* --- Tasking & Processes --- */
int  spawn_process(virt_addr_t entry_point, virt_addr_t user_stack);
int  fork();
void kill(int pid);
void ps();
int  getpid();
int  wait_for_children();
void reap_zombies();

/* --- Filesystem --- */
#include "vfs_core.h"
extern fs_node_t *fs_root;
uint64_t read_fs(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
struct dirent *readdir_fs(fs_node_t *node, uint64_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
void memory_copy(uint8_t *source, uint8_t *dest, size_t nbytes);
void memory_set(uint8_t *dest, uint8_t val, size_t len);

/* --- String / Util --- */
int strcmp(const char s1[], const char s2[]);
int strlen(const char s[]);

/* --- Scheduler API --- */
#include "task.h"
// set_scheduler is already declared in task.h

#endif
