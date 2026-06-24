# Patterns

> Reusable patterns and idioms discovered during development.

---

## SMP locking: `spin_lock_irqsave` / `spin_unlock_irqrestore` for shared data

When protecting shared mutable state under SMP, always use `spin_lock_irqsave(&lock)` / `spin_unlock_irqrestore(&lock, flags)` — not plain `irq_save()`/`irq_restore()`. The latter only masks interrupts on the local core and provides zero cross-core protection.

**Pattern:**
```c
static spinlock_t my_lock = SPINLOCK_INIT;

void my_function(void) {
    uint64_t f = spin_lock_irqsave(&my_lock);
    // ... critical section ...
    spin_unlock_irqrestore(&my_lock, f);
}
```

**When to use plain `spin_lock`/`spin_unlock` (without irqsave):**
Only when the critical section is already inside an interrupt handler (interrupts are already disabled), e.g. `rq_lock` in `task_switch()` which runs from the timer IRQ.

---

## Internal function split to avoid deadlock in nested calls

When function A holds a lock and calls function B which also needs the same lock, split B into:
- `B_internal()` — does the work, assumes lock is held
- `B()` — public wrapper that acquires the lock, calls `B_internal()`

**Example:** `free()` / `free_internal()` in `kheap.c`. The call chain `alloc() → expand() → free()` would deadlock because `alloc()` already holds `heap_lock`. Instead, `expand()` calls `free_internal()` directly.

---

## Conditional locking: protect shared directories, skip private ones

For structures that are sometimes shared (e.g. `kernel_directory`) and sometimes private (e.g. per-process page directories), guard the lock acquisition:

```c
int is_kernel = (ctx == kernel_directory);
if (is_kernel) spin_lock(&pgtable_lock);
// ... work ...
if (is_kernel) spin_unlock(&pgtable_lock);
```

This avoids unnecessary contention on the common path (user-space page faults on private directories) while protecting the genuinely shared structure.

---

## Atomic increments for shared counters

For simple shared counters hit by multiple cores (e.g. `tick` in `timer.c`), use GCC atomic builtins rather than a spinlock:

```c
uint32_t cur_tick = __sync_add_and_fetch(&tick, 1);
```

Consistent with the `__sync_` style already used for `ap_ready_flags` and `pending_mask` in `smp.c`.
