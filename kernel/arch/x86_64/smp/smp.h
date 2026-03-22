#pragma once

#include <stdint.h>

void smp_copy_trampoline(void);
void smp_start_aps(void);
void ap_entry(int cpu_id);
void lapic_send_ipi(uint8_t dest, uint32_t cmd);

extern volatile uint32_t ap_ready_flags;
