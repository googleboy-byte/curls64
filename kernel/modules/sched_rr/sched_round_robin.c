#include "../../../include/module/module_abi_v1.h"

// Note: In a real system, these would be opaque via K-ABI.
// Since we are same-binary for now, we cast.
#define TASK_READY 0

typedef struct {
    uint32_t magic;
    uint32_t id;
    virt_addr_t user_esp;
    virt_addr_t user_eip;
    virt_addr_t kernel_stack;
    virt_addr_t kernel_stack_base;
    void *page_directory;
    void *parent;
    volatile uint8_t state;
    uint32_t capabilities;
    void *fd_table[32]; // MAX_FD
    char cwd[512];
    void *next;
} task_struct_t;

extern volatile task_struct_t *ready_queue;
extern volatile task_struct_t *current_task;

extern void kprint(const char*);
extern void int_to_ascii(int, char*);
extern void hex_to_ascii(uint64_t, char*);

int round_robin_pick_next(kabi_task_t **out) {
    if (!ready_queue) {
        *out = (kabi_task_t*)current_task;
        return KABI_SUCCESS;
    }

    task_struct_t *next_task = current_task->next ? (task_struct_t*)current_task->next : (task_struct_t*)ready_queue;

    while (next_task->state != TASK_READY && next_task != (task_struct_t*)current_task) {
        next_task = next_task->next;
        if (!next_task) next_task = (task_struct_t*)ready_queue;
    }

    *out = (kabi_task_t*)next_task;
    return KABI_SUCCESS;
}

void round_robin_on_task_added(kabi_task_t *task) {
    (void)task;
}

void round_robin_on_task_removed(kabi_task_t *task) {
    (void)task;
}

static kabi_scheduler_ops_t sched_rr = {
    .name = "Round Robin",
    .version = 0x0100,
    .pick_next = round_robin_pick_next,
    .on_task_added = round_robin_on_task_added,
    .on_task_removed = round_robin_on_task_removed
};

void sched_rr_init() {
    kabi_scheduler_register(&sched_rr);
}

/* --- Module Registration --- */
static int sched_rr_module_init(void) { sched_rr_init(); return 0; }

kabi_module_t __kabi_module = {
    .name           = "sched_rr",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = sched_rr_module_init,
    .exit           = NULL,
    .description    = "Round-robin scheduler policy"
};
