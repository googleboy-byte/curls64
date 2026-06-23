#pragma once
#include "../core/task.h"

// Defined in gdt.c — array of per-CPU structs, currently size 1
extern cpu_local_t cpu_local[];


static inline void write_gs_base(uint64_t val) {
    asm volatile(
        "wrmsr"
        :: "c"(0xC0000101UL),
           "a"((uint32_t)(val & 0xFFFFFFFF)),
           "d"((uint32_t)(val >> 32))
    );
}

static inline cpu_local_t *get_cpu_local(void) {
    uint32_t lo, hi;
    asm volatile(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(0xC0000101UL)
    );
    return (cpu_local_t*)(((uint64_t)hi << 32) | lo);
}

// Convenience macros — use these in C code instead of bare globals.
// On UP these expand to cpu_local[0].field at zero extra cost.
// On SMP they will expand to the per-core struct automatically.
#define current_task      (get_cpu_local()->_current)
#define irq_depth         (get_cpu_local()->_irq_depth)
