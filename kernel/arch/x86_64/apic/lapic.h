#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

#define LAPIC_ID          0x020
#define LAPIC_VERSION     0x030
#define LAPIC_TPR         0x080  // Task Priority
#define LAPIC_EOI         0x0B0  // End of Interrupt
#define LAPIC_SVR         0x0F0  // Spurious Vector Register
#define LAPIC_ICR_LOW     0x300  // Interrupt Command (low)
#define LAPIC_ICR_HIGH    0x310  // Interrupt Command (high)
#define LAPIC_TIMER_LVT   0x320  // Timer LVT entry
#define LAPIC_TIMER_INIT  0x380  // Timer initial count
#define LAPIC_TIMER_CURR  0x390  // Timer current count
#define LAPIC_TIMER_DIV   0x3E0  // Timer divide config

extern int lapic_enabled;
extern int use_lapic_timer;

void lapic_init(void);
void init_lapic_timer(void);
void lapic_write(uint32_t reg, uint32_t val);
uint32_t lapic_read(uint32_t reg);
#include "../../../cpu/isr.h"
void lapic_spurious_handler(registers_t *regs);

#endif // LAPIC_H
