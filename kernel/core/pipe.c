#include "pipe.h"
#include <cpu_local.h>
#include "../../libc/mem.h"
#include "../../libc/string.h"
#include "task.h"

static int debug_pipe_count = 0;

int get_debug_pipe_count() {
    return debug_pipe_count;
}

pipe_t* pipe_create(uint32_t size) {
    pipe_t *p = (pipe_t*)kmalloc(sizeof(pipe_t), 0, 0);
    if (!p) return 0;
    memory_set((uint8_t*)p, 0, sizeof(pipe_t));
    
    p->size = size;
    {
        char s[20];
        kprint("[PIPE] Created at 0x"); hex64_to_ascii((uint64_t)p, s); kprint(s);
        kprint(" size="); int_to_ascii(p->size, s); kprint(s); kprint("\n");
    }
    p->buffer = (uint8_t*)kmalloc(p->size, 0, 0);
    if (!p->buffer) { kfree(p); return 0; }
    
    p->head = 0;
    p->tail = 0;
    p->len = 0;
    p->readers = 0;
    p->writers = 0;
    p->waiting_task = 0;
    
    spinlock_t init_lock = SPINLOCK_INIT;
    p->lock = init_lock;
    
    debug_pipe_count++;
    return p;
}

fs_node_t* pipe_create_node(pipe_t *p) {
    fs_node_t *node = (fs_node_t*)kmalloc(sizeof(fs_node_t), 0, 0);
    memory_set((uint8_t*)node, 0, sizeof(fs_node_t));
    strcpy(node->name, "pipe");
    node->flags = FS_PIPE | FS_TRANSIENT;
    node->impl = p;
    node->read = pipe_read;
    node->write = pipe_write;
    return node;
}

uint64_t pipe_read(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    pipe_t *p = (pipe_t*)node->impl;
    if (!p) return 0;
    (void)offset;

    uint64_t flags = spin_lock_irqsave(&p->lock);

    // Blocking logic: wait if empty but writers > 0
    while (p->len == 0 && p->writers > 0) {
        p->waiting_task = (void*)current_task;
        current_task->state = TASK_WAITING;
        
        spin_unlock_irqrestore(&p->lock, flags);
        
        // Wait for an interrupt (likely timer) to wake us up or switch tasks
        // This is similar to wait_for_children logic
        uint32_t f = irq_save();
        uint32_t saved_depth = irq_depth;
        irq_depth = 0;
        asm volatile("sti; hlt; cli");
        irq_depth = saved_depth;
        irq_restore(f);
        
        flags = spin_lock_irqsave(&p->lock);
        
        // Clean up: if we are woken up, we are no longer waiting on THIS specific pipe
        // (Another pipe might set it again if we loop)
        p->waiting_task = 0;
    }

    // EOF condition: buffer empty and no writers (after potential wait)
    if (p->len == 0 && p->writers == 0) {
        spin_unlock_irqrestore(&p->lock, flags);
        return 0;
    }
    
    uint32_t read_bytes = 0;
    while (read_bytes < size && p->len > 0) {
        buffer[read_bytes++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % p->size;
        p->len--;
    }
    spin_unlock_irqrestore(&p->lock, flags);
    return read_bytes;
}

uint64_t pipe_write(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer) {
    pipe_t *p = (pipe_t*)node->impl;
    if (!p) return 0;
    (void)offset;

    uint64_t flags = spin_lock_irqsave(&p->lock);

    // EPIPE condition: no readers
    if (p->readers == 0) {
        spin_unlock_irqrestore(&p->lock, flags);
        return (uint32_t)-1;
    }

    if (p->size == 0) {
        spin_unlock_irqrestore(&p->lock, flags);
        char s[20];
        kprint("[PIPE] !!! CRITICAL: pipe_write called with size=0! p=0x");
        hex64_to_ascii((uint64_t)p, s); kprint(s); kprint("\n");
        panic("PIPE SIZE ZERO");
    }

    uint32_t written_bytes = 0;
    while (written_bytes < size && p->len < p->size) {
        p->buffer[p->head] = buffer[written_bytes++];
        p->head = (p->head + 1) % p->size;
        p->len++;
    }

    // Wake up waiting reader!
    if (p->waiting_task) {
        task_t *t = (task_t*)p->waiting_task;
        if (t->state == TASK_WAITING) {
            t->state = TASK_READY;
        }
    }

    spin_unlock_irqrestore(&p->lock, flags);
    return written_bytes;
}

void pipe_add_reader(pipe_t *p) {
    if (!p) return;
    uint64_t flags = spin_lock_irqsave(&p->lock);
    p->readers++;
    spin_unlock_irqrestore(&p->lock, flags);
}

void pipe_add_writer(pipe_t *p) {
    if (!p) return;
    uint64_t flags = spin_lock_irqsave(&p->lock);
    p->writers++;
    spin_unlock_irqrestore(&p->lock, flags);
}

void pipe_remove_reader(pipe_t *p) {
    if (!p) return;
    uint64_t flags = spin_lock_irqsave(&p->lock);
    if (p->readers > 0) p->readers--;
    if (p->readers == 0 && p->writers == 0) {
        debug_pipe_count--;
    }
    spin_unlock_irqrestore(&p->lock, flags);
}

void pipe_remove_writer(pipe_t *p) {
    if (!p) return;
    uint64_t flags = spin_lock_irqsave(&p->lock);
    if (p->writers > 0) p->writers--;

    // If last writer left, wake up reader so they see EOF
    if (p->writers == 0 && p->waiting_task) {
        task_t *t = (task_t*)p->waiting_task;
        if (t->state == TASK_WAITING) {
            t->state = TASK_READY;
        }
    }

    if (p->readers == 0 && p->writers == 0) {
        debug_pipe_count--;
    }
    spin_unlock_irqrestore(&p->lock, flags);
}


int pipe(int fds[2]) {
    pipe_t *p = pipe_create(PIPE_SIZE);
    p->readers = 1;
    p->writers = 1;

    fs_node_t *node = pipe_create_node(p);

    // Allocate two file_t using the new factory
    file_t *rf = file_create(node, O_RDONLY);
    file_t *wf = file_create(node, O_WRONLY);

    // Find FDs
    int rfd = -1, wfd = -1;
    for (int i = FIRST_USER_FD; i < MAX_FD; i++) {
        if (!current_task->fd_table[i]) {
            if (rfd == -1) rfd = i;
            else if (wfd == -1) { wfd = i; break; }
        }
    }

    if (rfd == -1 || wfd == -1) {
        return -1;
    }

    current_task->fd_table[rfd] = rf;
    current_task->fd_table[wfd] = wf;

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}
