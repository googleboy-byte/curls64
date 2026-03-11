#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "task.h"

/* 
 * Scheduler Operations Interface
 * This defines the standard hooks that any scheduler module must implement.
 */
typedef struct {
    const char *name;
    uint32_t version;

    /* Primary hook: Pick the next task to run. 
     * Implementations should return a pointer to the task_t to switch to.
     */
    task_t* (*pick_next)();

    /* Event hook: Called when a task becomes READY and is added to the system. */
    void    (*on_task_added)(task_t *task);

    /* Event hook: Called when a task is removed (exited or killed). */
    void    (*on_task_removed)(task_t *task);

    /* optional: block / unblock hooks if we implement blocking later */
    // void (*on_task_blocked)(task_t *task);
} scheduler_ops_t;

/* Global pointer to the active scheduler */
extern scheduler_ops_t *current_scheduler;

/* Function to register/swap schedulers */
void set_scheduler(scheduler_ops_t *sched);

#endif
