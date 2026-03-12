#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>
#include "vfs_core.h"

#define PIPE_SIZE 4096

typedef struct {
    uint8_t *buffer;
    uint32_t head;
    uint32_t tail;
    uint32_t len;
    uint32_t size;
    uint32_t readers;
    uint32_t writers;
    void *waiting_task;
} pipe_t;

// Syscall implementation
int pipe(int fds[2]);
int get_debug_pipe_count();

// Internal Pipe API
pipe_t* pipe_create(uint32_t size);
fs_node_t* pipe_create_node(pipe_t *p);
#ifdef ARCH_X86_64
uint64_t pipe_read(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t pipe_write(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
#else
uint32_t pipe_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t pipe_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
#endif

void pipe_add_reader(pipe_t *p);
void pipe_add_writer(pipe_t *p);
void pipe_remove_reader(pipe_t *p);
void pipe_remove_writer(pipe_t *p);

#endif
