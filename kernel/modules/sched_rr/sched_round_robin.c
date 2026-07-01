#include "../../../include/module/module_abi_v1.h"

#include "../../core/task.h"
#include <cpu_local.h>

extern void kprint(const char*);
extern void int_to_ascii(int, char*);
extern void hex_to_ascii(uint64_t, char*);

extern volatile task_t *ready_queue;

int round_robin_pick_next(kabi_task_t **out) {
    if (!ready_queue) {
        *out = (kabi_task_t*)current_task;
        return KABI_SUCCESS;
    }

    task_t *next_task;
    if (!current_task) {
        next_task = (task_t*)ready_queue;
    } else {
        next_task = current_task->next ? (task_t*)current_task->next : (task_t*)ready_queue;
    }

    task_t *start_task = next_task;
    int first_pass = 1;
    int rotations = 0;
    while (next_task && (first_pass || next_task != start_task)) {
        first_pass = 0;
        if (next_task->state == TASK_READY &&
            (next_task->cpu_id == -1 ||
             next_task->cpu_id == (int32_t)get_cpu_local()->id)) {
            break;
        }
        next_task = next_task->next;
        if (!next_task) next_task = (task_t*)ready_queue;
        if (++rotations > MAX_TASKS + 2) break;
    }

    if (!next_task ||
        next_task->state != TASK_READY ||
        !(next_task->cpu_id == -1 ||
          next_task->cpu_id == (int32_t)get_cpu_local()->id)) {
        *out = (kabi_task_t*)current_task;
        return KABI_SUCCESS;
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
