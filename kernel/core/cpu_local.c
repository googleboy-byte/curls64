#include "../include/cpu_local.h"

volatile uint64_t task_switch_rsp = 0;  // asm-visible symbol
