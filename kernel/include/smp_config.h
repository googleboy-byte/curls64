#pragma once

/*
 * SMP_MAX_CPUS — canonical upper bound on supported CPUs.
 *
 * This constant governs:
 *   - Per-CPU arrays  (cpu_local[], ap_trampoline_stacks[])
 *   - Bitmask width   (ap_ready_flags, pending_mask are uint32_t → 32 max)
 *
 * Keep in sync: if you raise this beyond 32, the uint32_t bitmasks in
 * smp.c must be widened to uint64_t.
 */
#define SMP_MAX_CPUS 8
