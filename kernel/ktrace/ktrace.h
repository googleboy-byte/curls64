#ifndef KTRACE_H
#define KTRACE_H

#include <stdint.h>

/*
 * KTRACE: Persistent Kernel Trace for Crash Recovery
 * 
 * Events are stored in a ring buffer placed in a NOLOAD section.
 * On panic, state is snapshot. After soft reboot, we reconstruct.
 */

#define KTRACE_MAGIC        0x4B545243  /* "KTRC" */
#define KTRACE_VERSION      1
#define KTRACE_RING_SIZE    4096        /* Must be power of 2 */
#define KTRACE_RING_MASK    (KTRACE_RING_SIZE - 1)

/* Event IDs */
typedef enum {
    KTRACE_EVENT_NONE       = 0,
    
    /* IRQ Events */
    KTRACE_IRQ_ENTER        = 1,
    KTRACE_IRQ_EXIT         = 2,
    
    /* Scheduler Events */
    KTRACE_SCHED_SWITCH     = 10,
    KTRACE_TASK_CREATE      = 11,
    KTRACE_TASK_EXIT        = 12,
    
    /* Syscall Events */
    KTRACE_SYSCALL_ENTER    = 20,
    KTRACE_SYSCALL_EXIT     = 21,
    
    /* Fault Events */
    KTRACE_PAGE_FAULT       = 30,
    KTRACE_GPF              = 31,
    
    /* Panic marker */
    KTRACE_PANIC            = 99
} ktrace_event_id_t;

/* Single trace entry - 32 bytes, cache-line friendly */
typedef struct {
    uint32_t ts;            /* Timestamp (tick counter) */
    uint8_t  cpu;           /* CPU number (always 0 for single-core) */
    uint8_t  event_id;      /* Event type from enum */
    uint16_t flags;         /* Reserved for future use */
    uint32_t args[4];       /* Event-specific arguments */
    uint32_t _pad[2];       /* Pad to 32 bytes */
} __attribute__((packed)) ktrace_entry_t;

/* Panic snapshot metadata */
typedef struct {
    uint8_t  flag;          /* 1 = last boot crashed */
    uint8_t  cpu;           /* CPU where panic occurred */
    uint16_t _pad;
    uint32_t tsc;           /* Timestamp of panic */
    uint32_t head_snapshot; /* Ring head at moment of panic */
    char     msg[128];      /* Stored panic message */
} __attribute__((packed)) ktrace_panic_t;

/* Ring buffer state */
typedef struct {
    uint32_t size;          /* Always KTRACE_RING_SIZE */
    uint32_t mask;          /* Always KTRACE_RING_MASK */
    uint32_t head;          /* Monotonic write counter */
    ktrace_entry_t entries[KTRACE_RING_SIZE];
} ktrace_ring_t;

/* Complete ktrace state - lives in NOLOAD section */
typedef struct {
    uint32_t        magic;      /* Must be KTRACE_MAGIC */
    uint32_t        version;    /* Protocol version */
    ktrace_panic_t  panic;      /* Panic metadata */
    ktrace_ring_t   ring;       /* Event ring buffer */
} ktrace_state_t;

/* Event table entry for decoding */
typedef struct {
    uint8_t     id;
    const char *name;
    uint8_t     nargs;
    const char *arg_names[4];
} ktrace_event_info_t;

/* ============ API ============ */

/**
 * Initialize ktrace. Call early in kernel_main().
 * Checks for crash residue and replays if found.
 */
void ktrace_init(void);

/**
 * Record an event to the ring buffer.
 * @param event_id  Event type
 * @param a0-a3     Event-specific arguments
 */
void ktrace_event(uint8_t event_id, uint32_t a0, uint32_t a1, 
                  uint32_t a2, uint32_t a3);

/**
 * Snapshot panic state. Call from panic() before halt.
 */
void ktrace_panic_snapshot(char *msg);

/**
 * Replay last N events to console. Called on crash recovery.
 */
void ktrace_replay(uint32_t count);
int ktrace_is_active(void);

/* Convenience macros */
#define KTRACE0(id)              ktrace_event(id, 0, 0, 0, 0)
#define KTRACE1(id, a0)          ktrace_event(id, a0, 0, 0, 0)
#define KTRACE2(id, a0, a1)      ktrace_event(id, a0, a1, 0, 0)
#define KTRACE3(id, a0, a1, a2)  ktrace_event(id, a0, a1, a2, 0)

#endif /* KTRACE_H */
