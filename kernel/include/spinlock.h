#pragma once
#include <stdint.h>

#ifdef SMP
typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_lock(spinlock_t *l) {
    while (__sync_lock_test_and_set(&l->locked, 1)) {
        while (l->locked) {
            asm volatile("pause");
        }
    }
}

static inline void spin_unlock(spinlock_t *l) {
    __sync_lock_release(&l->locked);
}

static inline uint64_t spin_lock_irqsave(spinlock_t *l) {
    uint64_t flags;
    asm volatile("pushfq\npop %0\ncli" : "=r"(flags) :: "memory");
    spin_lock(l);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    spin_unlock(l);
    asm volatile("push %0\npopfq" :: "r"(flags) : "memory");
}

#else /* !SMP */

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
#ifdef ARCH_X86_64
    asm volatile("pushfq\npop %0\ncli" : "=r"(flags) :: "memory");
#else
    uint32_t f32;
    asm volatile("pushf\npop %0\ncli" : "=r"(f32) :: "memory");
    flags = f32;
#endif
    (void)l;
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    (void)l;
#ifdef ARCH_X86_64
    asm volatile("push %0\npopfq" :: "r"(flags) : "memory");
#else
    uint32_t f32 = (uint32_t)flags;
    asm volatile("push %0\npopf" :: "r"(f32) : "memory");
#endif
}

#endif /* !SMP */
