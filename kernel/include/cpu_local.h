#pragma once
#include "../core/task.h"

// Defined in gdt.c — array of per-CPU structs, currently size 1
extern cpu_local_t cpu_local[];

// task_switch_rsp: real global for asm visibility, macro for C consistency
extern volatile uint64_t task_switch_rsp;

#ifdef SMP
  // Future: read CPUID or GS base to get current CPU index
  #error "SMP get_cpu_local not yet implemented — Phase B work"
#else
  static inline cpu_local_t *get_cpu_local(void) {
      return &cpu_local[0];
  }
#endif

// Convenience macros — use these in C code instead of bare globals.
// On UP these expand to cpu_local[0].field at zero extra cost.
// On SMP they will expand to the per-core struct automatically.
#define current_task      (get_cpu_local()->_current)
#define irq_depth         (get_cpu_local()->_irq_depth)
