#include <kernel/kconfig.h>
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include <cpu_local.h>
#include <spinlock.h>

// Shell doesn't need core headers anymore!
// We'll use K-ABI functions only.

// Sysmon module functions
extern void sysmon_top(void);
extern void sysmon_ps_verbose(void);
extern void sysmon_mem_stats(void);
#include "../drivers/ide.h"

static int strcmp(char s1[], char s2[]) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') return 0;
    }
    return s1[i] - s2[i];
}

static int strlen(char s[]) {
    int i = 0;
    while (s[i] != '\0') ++i;
    return i;
}

static int startsWith(char *str, char *prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static int execute_elf(const char *path, int argc, char **argv, int wait) {
    int pid = kabi_fork();
    if (pid == 0) {
        // Child: Execute the new program
        int result;
        __asm__ __volatile__ (
            "int $0x80"
            : "=a" (result)
            : "a" (31), "b" (path), "c" (argv)
        );
        
        // If we reach here, exec failed
        if (kabi_debug_enabled()) {
            kprint("[CHILD] exec failed: ");
            kprint((char*)path);
            kprint("\n");
        }
        kabi_task_exit();
        return -1;
    } else if (pid > 0) {
        // Parent: Optionally wait for child
        if (wait) {
            kabi_wait_for_children();
        }
        return 0;
    } else {
        kprint("[SHELL] Fork failed.\n");
        return -1;
    }
}

void shell_user_input(char *input) {
    char *argv[16];
    int argc = 0;
    char *p = input;
    
    // Simple tokenizer
    while (*p && argc < 15) {
        while (*p == ' ') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    argv[argc] = 0;

    if (argc == 0) return;
    char *cmd = argv[0];

    if (strcmp(cmd, "HELP") == 0) {
        kprint("Curls OS K-ABI Shell\n"
               "Commands: \nHELP, \nCLEAR, \nDEVS, \nIDETEST, \nFATWRITE <path>, \nMOUNT <dev> <path>, \nLS <path>, \nCAT <path>, \nPS, \nMEM, \nTOP, \nMEMSTAT, \nPSV, \nTEST, \nSTRESS, \nCORE, \nUSER, \nKILL <pid>, \nTERM <pid>, \nDIAG, \nDEBUG <ON/OFF>, \nEND, \nREBOOT\n");
    } else if (strcmp(cmd, "DEBUG") == 0) {
        if (argc < 2) {
            kprint("DEBUG is "); kprint(kabi_debug_enabled() ? "ON\n" : "OFF\n");
        } else {
            if (strcmp(argv[1], "ON") == 0) kabi_set_debug(1);
            else if (strcmp(argv[1], "OFF") == 0) kabi_set_debug(0);
            else kprint("Usage: DEBUG <ON/OFF>\n");
        }
    } else if (strcmp(cmd, "CLEAR") == 0) {
        kabi_clear_screen();
    } else if (strcmp(cmd, "DEVS") == 0) {
        int count = kabi_get_device_count();
        char s[32];
        kprint("Block devices:\n");
        for (int i = 0; i < count; i++) {
            char name[32];
            if (kabi_get_device_name(i, name) == 0) {
                kprint("  dev");
                kabi_int_to_ascii(i, s); kprint(s);
                kprint(": ");
                kprint(name);
                uint64_t sectors = kabi_get_device_size(i);
                if (sectors > 0) {
                    uint64_t mb = sectors / 2048;
                    kprint("  (");
                    kabi_int_to_ascii(sectors, s); kprint(s);
                    kprint(" sectors, ");
                    if (mb > 0) {
                        kabi_int_to_ascii(mb, s); kprint(s);
                        kprint(" MB");
                    } else {
                        kabi_int_to_ascii(sectors / 2, s); kprint(s);
                        kprint(" KB");
                    }
                    kprint(")");
                }
                kprint("\n");
            }
        }
        kprint("\nUse: MOUNT <dev#> <path>  (e.g. MOUNT 2 /usb)\n");
    } else if (strcmp(cmd, "IDETEST") == 0) {
        uint8_t write_buf[512];
        uint8_t read_buf[512];
        for (int i = 0; i < 512; i++) write_buf[i] = i % 256;

        if (kabi_debug_enabled()) kprint("[IDETEST] Writing sector 500...\n");
        if (ide_write_sector(500, write_buf) != 0) {
            kprint("[IDETEST] Write FAILED\n");
        } else {
            if (kabi_debug_enabled()) kprint("[IDETEST] Reading sector 500...\n");
            memory_set(read_buf, 0, 512); // Clear buffer first
            if (ide_read_sector(500, read_buf) != 0) {
                kprint("[IDETEST] Read FAILED\n");
            } else {
                int match = 1;
                for (int i = 0; i < 512; i++) {
                    if (write_buf[i] != read_buf[i]) {
                        match = 0;
                        break;
                    }
                }
                if (kabi_debug_enabled()) {
                    if (match) kprint("[IDETEST] SUCCESS: Sector verified!\n");
                    else kprint("[IDETEST] FAILURE: Data mismatch!\n");
                }
            }
        }
    } else if (strcmp(cmd, "FATWRITE") == 0) {
        if (argc < 2) {
            kprint("Usage: FATWRITE <path>\n");
        } else {
            char *path = argv[1];
            kabi_fs_node_t *node = kabi_vfs_resolve_path(path);
            if (node) {
                char *msg = "KERNEL_WAS_HERE_2026";
                uint32_t len = strlen(msg);
                if (kabi_debug_enabled()) {
                    kprint("[FATWRITE] Overwriting start of "); kprint(path); kprint("...\n");
                }
                uint64_t written = kabi_vfs_write(node, 0, len, (uint8_t*)msg);
                if (written == len) {
                    if (kabi_debug_enabled()) {
                        char buf[32];
                        kprint("[FATWRITE] SUCCESS: wrote "); kabi_int_to_ascii(written, buf); kprint(buf); kprint(" bytes.\n");
                    }
                } else {
                    char buf[32];
                    kprint("[FATWRITE] FAILURE: only wrote "); kabi_int_to_ascii(written, buf); kprint(buf); kprint(" bytes.\n");
                }
            } else {
                kprint("[FATWRITE] File not found.\n");
            }
        }
    } else if (strcmp(cmd, "END") == 0) {
        if (kabi_request_shutdown() < 0) {
            kprint("Permission denied or shutdown failed.\n");
        }
    } else if (strcmp(cmd, "MOUNT") == 0) {
        if (argc < 3) {
            kprint("Usage: MOUNT <dev> <path>\n");
            kprint("  dev: device index (e.g. 2) or name (e.g. usb0, dev0)\n");
        } else {
            char *dev_str = argv[1];
            char *path_str = argv[2];
            int dev_id = -1;

            /* Try by name first */
            int count = kabi_get_device_count();
            for (int i = 0; i < count; i++) {
                char name[32];
                if (kabi_get_device_name(i, name) == 0) {
                    if (strcmp(dev_str, name) == 0) {
                        dev_id = i;
                        break;
                    }
                }
            }

            /* Try as "devN" */
            if (dev_id < 0 && dev_str[0] == 'd' && dev_str[1] == 'e' && dev_str[2] == 'v') {
                dev_id = dev_str[3] - '0';
            }

            /* Try as plain number */
            if (dev_id < 0 && dev_str[0] >= '0' && dev_str[0] <= '9') {
                dev_id = dev_str[0] - '0';
            }

            if (dev_id < 0 || dev_id >= count) {
                kprint("Unknown device: "); kprint(dev_str); kprint("\n");
            } else {
                extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
                if (fat32_vfs_mount(dev_id, path_str) == 0) {
                    kprint("Mounted "); kprint(dev_str); kprint(" at "); kprint(path_str); kprint("\n");
                } else {
                    kprint("Mount failed (device may not have a FAT32 partition).\n");
                }
            }
        }
    } else if (strcmp(cmd, "LS") == 0) {
        char *path = "/";
        if (argc > 1) path = argv[1];

        kabi_fs_node_t *node = kabi_vfs_resolve_path(path);

        if (node) {
            int i = 0;
            kabi_dirent_t *ent;
            while ((ent = kabi_vfs_readdir(node, i++)) != 0) {
                kprint(ent->name);
                kprint("\n");
                kfree(ent);
            }
        } else {
            kprint("Path not found.\n");
        }
    } else if (strcmp(cmd, "CAT") == 0) {
        if (argc < 2) {
             kprint("Usage: CAT <path>\n");
        } else {
            char *path = argv[1];
            kabi_fs_node_t *node = kabi_vfs_resolve_path(path);
            
            if (node) {
                uint8_t buf[513];
                uint64_t offset = 0;
                uint64_t sz;
                while ((sz = kabi_vfs_read(node, offset, 512, buf)) > 0) {
                    buf[sz] = 0;
                    kprint((char*)buf);
                    offset += sz;
                }
                kprint("\n");
            } else {
                kprint("File not found.\n");
            }
        }

        // PROC/MEM COMMANDS
    } else if (strcmp(cmd, "EXEC") == 0) {
        if (argc < 2) {
            kprint("Usage: EXEC <path> [args...]\n");
        } else {
            execute_elf(argv[1], argc - 1, &argv[1], 1); // wait=1
        }
    } else if (strcmp(cmd, "PS") == 0) {
        kabi_ps();
    } else if (strcmp(cmd, "MEM") == 0) {
        kabi_heap_stats_t heap;
        kabi_pmm_stats_t pmm;
        kabi_get_heap_stats(&heap);
        kabi_get_pmm_stats(&pmm);
        
        kprint("--- Memory Statistics ---\n");
        kprint("Kernel Heap: ");
        char buf[32];
        kabi_int_to_ascii(heap.used_size / 1024, buf); kprint(buf); kprint("/");
        kabi_int_to_ascii(heap.total_size / 1024, buf); kprint(buf); kprint(" KB used\n");
        
        kprint("Physical RAM: ");
        kabi_int_to_ascii(pmm.used_frames * 4, buf); kprint(buf); kprint("/");
        kabi_int_to_ascii(pmm.total_frames * 4, buf); kprint(buf); kprint(" KB used\n");

        // SYS MON COMMANDS
    } else if (strcmp(input, "TOP") == 0) {
        sysmon_top();
    } else if (strcmp(input, "MEMSTAT") == 0) {
        sysmon_mem_stats();
    } else if (strcmp(input, "PSV") == 0) {
        sysmon_ps_verbose();

        // TEST/STRESS COMMANDS
    } else if (strcmp(cmd, "TEST") == 0) {
#ifdef KABI_DEBUG
        kabi_debug_run_test(KABI_DEBUG_TEST_PAGING, argc > 1 ? argv[1] : "");
#else
        kprint("Debug tests disabled in production ABI.\n");
#endif
    } else if (strcmp(cmd, "STRESS") == 0) {
#ifdef KABI_DEBUG
        kabi_debug_run_test(KABI_DEBUG_TEST_STRESS, argc > 1 ? argv[1] : "");
#else
        kprint("Stress tests disabled in production ABI.\n");
#endif
    } else if (strcmp(cmd, "CORE") == 0) {
#ifdef KABI_DEBUG
        kabi_debug_run_test(KABI_DEBUG_TEST_CORE, "");
#else
        kprint("Core tests disabled in production ABI.\n");
#endif
    } else if (strcmp(input, "USER") == 0) {
        extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
        
        // Auto-mount dev0 if /BIN/INIT.ELF is not found
        if (!kabi_vfs_resolve_path("/BIN/INIT.ELF")) {
            if (kabi_debug_enabled()) kprint("[USER] Mounting dev0 at / ...\n");
            if (fat32_vfs_mount(0, "/") != 0) {
                kprint("[USER] Mount failed! Cannot proceed.\n");
                return;
            }
        }

        if (kabi_debug_enabled()) kprint("[USER] Executing /BIN/INIT.ELF (Background) ...\n");
        static char *init_argv[] = {"/BIN/INIT.ELF", 0};
        execute_elf("/BIN/INIT.ELF", 1, init_argv, 0); // wait=0 (Background)
        /* NOTE: INIT.ELF execs into /BIN/SH.ELF (the 64-bit shell).
         * Do NOT launch SH64.ELF separately — it doesn't exist on FAT32
         * (Makefile copies sh64.elf as SH.ELF). A phantom child would
         * die instantly as a zombie, causing wait_for_children() to
         * return prematurely and let KABI compete for keyboard input. */
        kprint("[USER] Single child launched (INIT->SH). KABI will block until exit.\n");
    } else if (startsWith(input, "KILL ")) {
        int pid = 0;
        char *p = input + 5;
        while (*p >= '0' && *p <= '9') {
            pid = pid * 10 + (*p - '0');
            p++;
        }
        if (kabi_task_signal(pid, KABI_SIGNAL_KILL) < 0) {
            kprint("Permission denied or kill failed.\n");
        }
    } else if (startsWith(input, "TERM ")) {
        int pid = 0;
        char *p = input + 5;
        while (*p >= '0' && *p <= '9') {
            pid = pid * 10 + (*p - '0');
            p++;
        }
        if (kabi_task_signal(pid, KABI_SIGNAL_TERM) < 0) {
            kprint("Permission denied or termination failed.\n");
        }
    } else if (strcmp(cmd, "DIAG") == 0) {
        /* ===== AP-Stall & SMP Audit Diagnostic ===== */
        extern uint32_t tick;
        extern uint32_t next_pid;
        extern int smp_tasking_ready;
        char _s[32];

        kprint("\n========== SMP AUDIT DIAGNOSTIC v2 ==========\n");

        /* ============================================
         * SECTION 1: Per-CPU Heartbeat & State
         * ============================================ */
        kprint("\n[AP-STALL] Per-CPU State Dump\n");
        kprint("  CPU  timer_ticks   PID   irq_depth  tss_rsp  status\n");
        kprint("  ---  -----------   ---   ---------  -------  ------\n");

        int ncpus = 0;
        for (int i = 0; i < 8; i++) {
            if (i == 0 || cpu_local[i].kstack_top != 0) ncpus++;
            else break;
        }

        /* Sample 1: record timer_ticks */
        uint64_t sample1[8];
        for (int i = 0; i < ncpus; i++) sample1[i] = cpu_local[i].timer_ticks;

        for (int i = 0; i < ncpus; i++) {
            kprint("    "); kabi_int_to_ascii(i, _s); kprint(_s);
            kprint("    ");
            kabi_int_to_ascii((int)cpu_local[i].timer_ticks, _s); kprint(_s);

            /* Current task PID */
            task_t *ct = cpu_local[i]._current;
            kprint("         ");
            if (ct) { kabi_int_to_ascii(ct->id, _s); kprint(_s); }
            else kprint("NULL");

            /* irq_depth */
            kprint("     ");
            kabi_int_to_ascii(cpu_local[i]._irq_depth, _s); kprint(_s);

            /* task_switch_rsp */
            kprint("          ");
            if (cpu_local[i]._task_switch_rsp) kprint("set");
            else kprint("0");

            /* Status */
            kprint("    ");
            if (i == 0) { kprint("BSP"); }
            else if (cpu_local[i]._irq_depth > 0) { kprint("STUCK-IN-IRQ"); }
            else if (ct && ct->state == TASK_ZOMBIE) { kprint("ZOMBIE-TASK"); }
            else { kprint("ok"); }
            kprint("\n");
        }

        /* Sample 2: wait ~500ms on BSP, re-sample */
        kprint("\n[AP-STALL] Liveness probe (waiting ~500ms)...\n");
        {
            uint64_t bsp_start = cpu_local[0].timer_ticks;
            volatile uint32_t *irq_d = &(get_cpu_local()->_irq_depth);
            uint32_t saved_depth = *irq_d;
            *irq_d = 0;
            /* Wait for BSP to tick 25 times (~500ms at 50 Hz) */
            while (cpu_local[0].timer_ticks < bsp_start + 25) {
                asm volatile("sti; hlt; cli");
            }
            *irq_d = saved_depth;
        }

        uint64_t sample2[8];
        for (int i = 0; i < ncpus; i++) sample2[i] = cpu_local[i].timer_ticks;

        int stalled_count = 0;
        for (int i = 0; i < ncpus; i++) {
            uint64_t delta = sample2[i] - sample1[i];
            kprint("  CPU "); kabi_int_to_ascii(i, _s); kprint(_s);
            kprint(": delta="); kabi_int_to_ascii((int)delta, _s); kprint(_s);
            if (i == 0) {
                kprint(" (BSP, expected ~25)");
            } else if (delta == 0) {
                kprint(" >> STALLED: no timer interrupts in 500ms");
                stalled_count++;
            } else if (delta < 10) {
                kprint(" >> SLOW: barely ticking");
            } else {
                kprint(" (OK)");
            }
            kprint("\n");
        }

        if (stalled_count > 0) {
            kprint("\n  >> "); kabi_int_to_ascii(stalled_count, _s); kprint(_s);
            kprint(" AP(s) STALLED. Possible causes:\n");
            kprint("     - Deadlock on rq_lock, sq_lock, kprint_lock, or heap_lock\n");
            kprint("     - Silent panic (kprint_lock held -> panic never printed)\n");
            kprint("     - LAPIC timer stopped or never configured\n");
            kprint("     - Wedged in task_switch spin or irq_depth > 1 rejection\n");
        }

        /* ============================================
         * SECTION 2: Spinlock State
         * ============================================ */
        kprint("\n[LOCKS] Spinlock Contention Check\n");
        {
            extern volatile uint32_t rq_lock;
            extern volatile uint32_t sq_lock;
            extern volatile uint32_t kprint_lock;

            kprint("  rq_lock:     ");
            kprint(rq_lock ? "LOCKED  << AP may be spinning here" : "free");
            kprint("\n  sq_lock:     ");
            kprint(sq_lock ? "LOCKED  << timer_callback contention" : "free");
            kprint("\n  kprint_lock: ");
            kprint(kprint_lock ? "LOCKED  (expected: we hold it to print)" : "free (unexpected)");
            kprint("\n");
            kprint("  pmm_lock, heap_lock, pid_lock, pgtable_lock: static, not readable\n");
            kprint("  >> Also check serial log for 'KERNEL PANIC' or 'EXCESSIVE IRQ'\n");
        }

        /* ============================================
         * SECTION 3: Timer frequency audit
         * ============================================ */
        kprint("\n[TIMER] Frequency Audit\n");
        kprint("  PIT init_timer() called with: 50 Hz\n");
        kprint("  UABI_SLEEP comment says:      100 Hz\n");
        kprint("  UABI_SLEEP formula: ticks = (ms + 9) / 10\n");
        kprint("  Correct formula at 50 Hz: ticks = (ms * 50 + 999) / 1000\n");
        kprint("  >> BUG: sleep durations are ~2x too short even with BSP-only tick\n");

        /* ============================================
         * SECTION 4: H4 tick drift
         * ============================================ */
        kprint("\n[H4] Tick Drift\n");
        kprint("  Global tick: "); kabi_int_to_ascii(tick, _s); kprint(_s); kprint("\n");
        if (cpu_local[0].timer_ticks > 0) {
            int ratio = (int)(tick / (uint32_t)cpu_local[0].timer_ticks);
            kprint("  BSP ticks:   "); kabi_int_to_ascii((int)cpu_local[0].timer_ticks, _s); kprint(_s); kprint("\n");
            kprint("  Ratio:       ~"); kabi_int_to_ascii(ratio, _s); kprint(_s);
            kprint("x (should be 1x)\n");
        }

        /* ============================================
         * SECTION 5: Existing checks
         * ============================================ */
        kprint("\n[H6] PID Space\n");
        kprint("  next_pid="); kabi_int_to_ascii(next_pid, _s); kprint(_s);
        kprint("  MAX_TASKS=128  remaining=");
        int remaining = 128 - (int)next_pid;
        kabi_int_to_ascii(remaining < 0 ? 0 : remaining, _s); kprint(_s); kprint("\n");

        kprint("\n[M7] PMM Frame Snapshot\n");
        {
            kabi_pmm_stats_t pmm;
            kabi_get_pmm_stats(&pmm);
            kprint("  Used="); kabi_int_to_ascii(pmm.used_frames, _s); kprint(_s);
            kprint("  Free="); kabi_int_to_ascii(pmm.free_frames, _s); kprint(_s);
            kprint("  Total="); kabi_int_to_ascii(pmm.total_frames, _s); kprint(_s);
            kprint("\n");
        }

        kprint("\n[M9] KILL/TERM: ");
        {
            char t1[] = "KILL 5";
            int pre = startsWith(t1, "KILL ");
            char *tp = t1;
            while (*tp) { if (*tp == ' ') { *tp = '\0'; break; } tp++; }
            int post = startsWith(t1, "KILL ");
            kprint(pre && !post ? "BUG CONFIRMED\n" : "OK\n");
        }

        kprint("\n========== END DIAGNOSTIC v2 ==========\n\n");

    } else if (strcmp(input, "REBOOT") == 0) {
        kabi_request_reboot(REBOOT_REASON_ADMIN);
    } else {
        kprint("Unknown command: ");
        kprint(cmd);
        kprint("\n");
    }
}

void shell_init() {
    kabi_clear_screen();
    kprint("\n[SHELL] Curls OS K-ABI Shell v2\n");
}

void kernel_shell() {
#ifdef KABI_DEBUG
    shell_user_input("CORE");
    shell_user_input("USER"); // DIAGNOSTIC run

    /* Block until ALL userland children exit.
     * Uses wait_for_all_children() which drains stale test zombies
     * before blocking on the live INIT->SH child.                  */
    kprint("[KABI] Blocking on all userland children...\n");
    kabi_wait_for_all_children();
    kprint("[KABI] All children exited. KABI shell resuming.\n");

    char input[256];
    while (1) {
        kprint("(KABI)> ");
        kabi_get_line(input);
        shell_user_input(input);
    }
#else
    shell_user_input("USER");
    shell_user_input("END");

    while (1) {
        asm volatile("hlt");
    }
#endif
}

/* --- Module Registration --- */
static int shell_module_init(void) { shell_init(); return 0; }

kabi_module_t __kabi_module_shell = {
    .name           = "shell",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = shell_module_init,
    .exit           = NULL,
    .description    = "K-ABI interactive shell"
};
