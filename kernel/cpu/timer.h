#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "isr.h"

void init_timer(uint32_t freq);
void timer_callback(registers_t *regs);
uint32_t get_ticks();

#endif
