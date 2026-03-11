#include "ktrace.h"
#include "../modules/drivers/screen.h"
#include "../../libc/string.h"
#include "../cpu/timer.h"
#include <stddef.h>

/*
 * KTRACE: Persistent Kernel Trace Implementation
 * 
 * The ktrace_state lives in a NOLOAD section at 0x400000 (4MB).
 * This memory is never zeroed by the linker or C runtime,
 * so it survives soft reboots.
 */

/* Extern tick counter from timer.c */
extern uint32_t tick;

/* The persistent state - placed in .ktrace NOLOAD section */
volatile ktrace_state_t ktrace_state __attribute__((section(".ktrace")));

/* Event table for decoding */
static const ktrace_event_info_t event_table[] = {
    { KTRACE_EVENT_NONE,    "NONE",         0, {NULL} },
    { KTRACE_IRQ_ENTER,     "IRQ_ENTER",    1, {"irq"} },
    { KTRACE_IRQ_EXIT,      "IRQ_EXIT",     1, {"irq"} },
    { KTRACE_SCHED_SWITCH,  "SCHED_SWITCH", 2, {"prev_pid", "next_pid"} },
    { KTRACE_TASK_CREATE,   "TASK_CREATE",  1, {"new_pid"} },
    { KTRACE_TASK_EXIT,     "TASK_EXIT",    2, {"pid", "exit_code"} },
    { KTRACE_SYSCALL_ENTER, "SYSCALL_ENTER",1, {"syscall_num"} },
    { KTRACE_SYSCALL_EXIT,  "SYSCALL_EXIT", 2, {"syscall_num", "ret"} },
    { KTRACE_PAGE_FAULT,    "PAGE_FAULT",   2, {"addr", "error_code"} },
    { KTRACE_GPF,           "GPF",          1, {"error_code"} },
    { KTRACE_PANIC,         "PANIC",        0, {NULL} },
    { 0xFF, NULL, 0, {NULL} }  /* Sentinel */
};

/* Find event info by ID */
static const ktrace_event_info_t* find_event_info(uint8_t id) {
    for (int i = 0; event_table[i].name != NULL; i++) {
        if (event_table[i].id == id) {
            return &event_table[i];
        }
    }
    return NULL;
}

/* Print a single trace entry */
static void print_entry(ktrace_entry_t *e, uint32_t idx) {
    char buf[16];
    const ktrace_event_info_t *info = find_event_info(e->event_id);
    
    /* Format: [idx] ts: EVENT_NAME arg0=val0 arg1=val1 ... */
    kprint("  [");
    int_to_ascii(idx, buf);
    kprint(buf);
    kprint("] ts=");
    int_to_ascii(e->ts, buf);
    kprint(buf);
    kprint(" ");
    
    if (info) {
        kprint((char*)info->name);
        for (int i = 0; i < info->nargs && i < 4; i++) {
            kprint(" ");
            kprint((char*)info->arg_names[i]);
            kprint("=0x");
            hex_to_ascii(e->args[i], buf);
            kprint(buf);
        }
    } else {
        kprint("UNKNOWN(");
        int_to_ascii(e->event_id, buf);
        kprint(buf);
        kprint(")");
    }
    kprint("\n");
}

/*
 * Initialize ktrace.
 * Check for crash residue via magic number.
 */
void ktrace_init(void) {
    kprint("[KTRACE] Initializing persistent tracer at 0x400000...\n");
    
    char buf[16];
    kprint("  - Magic: 0x"); hex_to_ascii(ktrace_state.magic, buf); kprint(buf);
    kprint(" Flag: "); int_to_ascii(ktrace_state.panic.flag, buf); kprint(buf);
    kprint("\n");

    /* Check if we have valid trace memory */
    if (ktrace_state.magic == KTRACE_MAGIC && ktrace_state.version == KTRACE_VERSION) {
        if (ktrace_state.panic.flag) {
            kprint("[KTRACE] !!! CRASH DETECTED from previous boot !!!\n");
            if (ktrace_state.panic.msg[0] != '\0') {
                kprint("[KTRACE] Message: ");
                kprint((char*)ktrace_state.panic.msg);
                kprint("\n");
            }
            kprint("[KTRACE] Type 'KLOG' to replay the failure trace.\n");
        } else {
            kprint("[KTRACE] Valid trace buffer found (clean reboot).\n");
            /* We continue tracing where we left off or keep historical data.
               Actually, if it's a clean reboot, maybe just reset head or keep it?
               Let's keep it to see historical events across healthy reboots too! 
               But we must ensure ring metadata is valid. */
            if (ktrace_state.ring.size != KTRACE_RING_SIZE) {
                kprint("[KTRACE] Metadata mismatch, re-initializing.\n");
                goto init_fresh;
            }
        }
    } else {
    init_fresh:
        /* Fresh boot or corrupted data - initialize */
        kprint("[KTRACE] Fresh boot or invalid magic. Initializing buffer.\n");
        
        ktrace_state.magic = KTRACE_MAGIC;
        ktrace_state.version = KTRACE_VERSION;
        ktrace_state.panic.flag = 0;
        ktrace_state.panic.cpu = 0;
        ktrace_state.panic.tsc = 0;
        ktrace_state.panic.head_snapshot = 0;
        
        ktrace_state.ring.size = KTRACE_RING_SIZE;
        ktrace_state.ring.mask = KTRACE_RING_MASK;
        ktrace_state.ring.head = 0;
        
        /* Zero the ring buffer */
        for (uint32_t i = 0; i < KTRACE_RING_SIZE; i++) {
            ktrace_state.ring.entries[i].event_id = 0;
            ktrace_state.ring.entries[i].ts = 0;
        }
    }
    
    kprint("[KTRACE] Ready. Ring head: "); 
    int_to_ascii(ktrace_state.ring.head, buf); kprint(buf);
    kprint("\n");
}

/*
 * Record an event to the ring buffer.
 * This is designed to be fast and lockless (single CPU).
 */
void ktrace_event(uint8_t event_id, uint32_t a0, uint32_t a1,
                  uint32_t a2, uint32_t a3) {
    uint32_t idx = ktrace_state.ring.head & KTRACE_RING_MASK;
    ktrace_entry_t *e = (ktrace_entry_t*)&ktrace_state.ring.entries[idx];
    
    e->ts = tick;  /* Use tick counter as timestamp */
    e->cpu = 0;    /* Single CPU for now */
    e->event_id = event_id;
    e->flags = 0;
    e->args[0] = a0;
    e->args[1] = a1;
    e->args[2] = a2;
    e->args[3] = a3;
    
    /* Monotonically increment head - this is our sequence number */
    ktrace_state.ring.head++;
}

/*
 * Snapshot state on panic.
 * Called from panic() before halting.
 */
/*
 * Snapshot state on panic.
 * Called from panic() before halting.
 */
void ktrace_panic_snapshot(char *msg) {
    /* Record final PANIC event */
    ktrace_event(KTRACE_PANIC, 0, 0, 0, 0);
    
    /* Snapshot the state */
    ktrace_state.panic.flag = 1;
    ktrace_state.panic.cpu = 0;
    ktrace_state.panic.tsc = tick;
    ktrace_state.panic.head_snapshot = ktrace_state.ring.head;
    
    /* Store the message */
    if (msg) {
        int i;
        for (i = 0; i < 127 && msg[i] != '\0'; i++) {
            ktrace_state.panic.msg[i] = msg[i];
        }
        ktrace_state.panic.msg[i] = '\0';
    } else {
        ktrace_state.panic.msg[0] = '\0';
    }
    
    /* State is now frozen. RAM will preserve it across soft reboot. */
}

/*
 * Replay the last N events from the ring buffer.
 * Used on crash recovery to show what happened.
 */
void ktrace_replay(uint32_t count) {
    if (!ktrace_state.panic.flag && ktrace_state.ring.head == 0) {
        kprint("  (no crash data or events recorded)\n");
        return;
    }

    uint32_t head = (ktrace_state.panic.flag) ? ktrace_state.panic.head_snapshot : ktrace_state.ring.head;
    
    /* Don't try to read more than we have */
    if (head < count) {
        count = head;
    }
    
    /* Also cap at ring size */
    if (count > KTRACE_RING_SIZE) {
        count = KTRACE_RING_SIZE;
    }
    
    kprint("--- KTRACE REPLAY (Last ");
    char cs[10]; int_to_ascii(count, cs); kprint(cs);
    kprint(" events) ---\n");

    if (ktrace_state.panic.flag && ktrace_state.panic.msg[0] != '\0') {
        kprint("PANIC MESSAGE: ");
        kprint((char*)ktrace_state.panic.msg);
        kprint("\n");
    }

    /* Walk backwards from head */
    for (uint32_t i = count; i > 0; i--) {
        uint32_t seq = head - i;
        uint32_t slot = seq & KTRACE_RING_MASK;
        ktrace_entry_t *e = (ktrace_entry_t*)&ktrace_state.ring.entries[slot];
        
        /* Only print if this slot has valid data */
        if (e->event_id != 0) {
            print_entry(e, seq);
        }
    }

    /* If we were replaying a crash, clear the flag now so we don't nag user every boot */
    if (ktrace_state.panic.flag) {
        kprint("[KTRACE] Crash data consumed.\n");
        ktrace_state.panic.flag = 0;
    }
    kprint("--- END OF TRACE ---\n");
}

int ktrace_is_active(void) {
    return (ktrace_state.magic == KTRACE_MAGIC);
}
