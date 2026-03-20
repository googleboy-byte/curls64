#pragma once
#include <stdint.h>

#ifdef SMP
  #error "SMP spinlock not yet implemented — Phase B work"
#else

typedef struct { } spinlock_t;
#define SPINLOCK_INIT {}

static inline void spin_lock(spinlock_t *l) {
    (void)l;
    asm volatile("cli" ::: "memory");
}

static inline void spin_unlock(spinlock_t *l) {
    (void)l;
    asm volatile("sti" ::: "memory");
}

static inline uint64_t spin_lock_irqsave(spinlock_t *l) {
    uint64_t flags;
    asm volatile("pushfq\npop %0\ncli" : "=r"(flags) :: "memory");
    (void)l;
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    (void)l;
    asm volatile("push %0\npopfq" :: "r"(flags) : "memory");
}

#endif /* !SMP */
