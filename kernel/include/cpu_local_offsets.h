#pragma once
#include <stddef.h>
#include "../core/task.h"

// This constant MUST match the %define in interrupt64.asm.
// If the struct layout changes, this _Static_assert will fire.
//
// Long-term: generate this automatically during the build
// (like Linux's asm-offsets.h) so assembly picks up the offset
// from a generated header. The assert is a safety net until then.
#define CPU_LOCAL_TASK_SWITCH_RSP_OFFSET 40

_Static_assert(
    offsetof(cpu_local_t, _task_switch_rsp) == CPU_LOCAL_TASK_SWITCH_RSP_OFFSET,
    "cpu_local_t layout changed: update CPU_LOCAL_TASK_SWITCH_RSP in interrupt64.asm"
);
