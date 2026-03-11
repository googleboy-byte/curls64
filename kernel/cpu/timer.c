#include "timer.h"
#include "isr.h"
#include "ports.h"
#include "../../libc/function.h"
#include "../core/task.h"

uint32_t tick = 0;

extern volatile task_t *ready_queue;

static void timer_callback(registers_t *regs) {
    (void)regs;
    tick++;
    if (current_task) ((task_t*)current_task)->ticks++;

    /* Drain the head of the sorted sleep queue */
    extern volatile task_t *sleep_queue;
    while (sleep_queue && tick >= sleep_queue->sleep_until) {
        task_t *t = (task_t*)sleep_queue;
        sleep_queue = t->sleep_next;
        t->sleep_next  = NULL;
        t->sleep_until = 0;
        if (t->state == TASK_WAITING) t->state = TASK_READY;
    }
}

void init_timer(uint32_t freq) {
    /* Install the function we just wrote */
    register_interrupt_handler(IRQ0, timer_callback);

    /* Get the PIT value: hardware clock at 1193180 Hz */
    uint32_t divisor = 1193180 / freq;
    uint8_t low  = (uint8_t)(divisor & 0xFF);
    uint8_t high = (uint8_t)( (divisor >> 8) & 0xFF);
    /* Send the command */
    port_byte_out(0x43, 0x36); /* Command port */
    port_byte_out(0x40, low);
    port_byte_out(0x40, high);
}

uint32_t get_ticks() {
    return tick;
}

