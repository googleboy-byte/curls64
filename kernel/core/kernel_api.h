#ifndef KERNEL_API_H
#define KERNEL_API_H

/**
 * Curls Kernel stable API
 * This header defines the "Sacred Layer" surface area available to modules.
 */

#include <stdint.h>
#include <stddef.h>

/* --- Memory Management --- */
void *kmalloc(uint32_t size, int align, uint32_t *phys);
void kfree(void *p);

typedef struct {
    uint32_t total_size;
    uint32_t used_size;
    uint32_t free_size;
    uint32_t max_addr;
} heap_stats_t;

void get_heap_stats(heap_stats_t *stats);

/* --- Output / Debugging --- */
void kprint(char *c);
void kprint_at(char *c, int col, int row);
void clear_screen();
void int_to_ascii(int n, char str[]);
void hex_to_ascii(uint32_t n, char str[]);

/* --- Tasking & Processes --- */
int  spawn_process(uint32_t entry_point, uint32_t user_stack);
int  fork();
void kill(int pid);
void ps();
int  getpid();
int  wait_for_children();
void reap_zombies();

/* --- Filesystem --- */
#include "vfs_core.h"
extern fs_node_t *fs_root;
uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
struct dirent *readdir_fs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);

/* --- String / Util --- */
int strcmp(const char s1[], const char s2[]);
int strlen(const char s[]);
void memory_copy(uint8_t *source, uint8_t *dest, int nbytes);
void memory_set(uint8_t *dest, uint8_t val, uint32_t len);

/* --- Scheduler API --- */
#include "task.h"
// set_scheduler is already declared in task.h

#endif
