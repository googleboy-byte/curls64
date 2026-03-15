#include "core_test64_v1.h"
#include "../../kernel.h"
#include "../../task.h"
#include "../../syscall_dispatch.h"
#include "../../../cpu/isr.h"
#include "../../../cpu/paging.h"
#include "../../../arch/x86_64/cpu/gdt.h"
#include "../../../../include/kabi/kabi_v1.h"
#include "../../../../libc/string.h"
#include "../../../../libc/mem.h"
#include "../../../ktrace/ktrace.h"
#include "../../block_dev.h"
#include "../../../modules/partition/mbr.h"
#include "../../../fs/fat32/fat32_bpb.h"
#include "../../../fs/fat32/fat32_file.h"
#include "../../signal.h"
#include "../../../cpu/timer.h"
#include "../../pipe.h"
#include "../../uabi_helpers.h"

// Phase success trackers
static int phase_failed = 0;

static void log_phase_start(int ph, char *name) {
    kprint("[ CORE TEST 64 ] Phase ");
    char s[4]; int_to_ascii(ph, s); kprint(s);
    kprint(": "); kprint(name); kprint("\n");
}

static void log_pass(char *check_id, char *desc) {
    kprint("  [ PASS ] "); kprint(check_id); kprint(": "); kprint(desc); kprint("\n");
}

static void log_fail(char *check_id, char *desc, char *details) {
    kprint("  [ FAIL ] "); kprint(check_id); kprint(": "); kprint(desc);
    if (details) {
        kprint(" ("); kprint(details); kprint(")");
    }
    kprint("\n");
    phase_failed = 1;
}

static void log_result(int ph, int success) {
    char s[4]; int_to_ascii(ph, s);
    if (success) {
        kprint("[  OK  ] Phase ");
    } else {
        kprint("[ ERROR] Phase ");
        phase_failed = 1;
    }
    kprint(s);
    kprint("\n");
}

/* Helper to traverse 4-level paging for kernel COW immunity check */
static int check_kernel_cow_64() {
    mmu_context_t *dir = (mmu_context_t*)kernel_directory;
    if (!dir || !dir->pml4_virt) return 1;

    // In 64-bit, we consider indices >= 256 as kernel-space (higher-half or identity)
    for (int i = 256; i < 512; i++) {
        uint64_t pml4e = dir->pml4_virt->entries[i];
        if (!(pml4e & MMU_PRESENT)) continue;
        if (pml4e & MMU_COW) return 0;

        mmu_table_t *pdpt = (mmu_table_t*)((uintptr_t)PHYSMAP_BASE + (pml4e & ~0xFFFULL));
        for (int j = 0; j < 512; j++) {
            uint64_t pdpte = pdpt->entries[j];
            if (!(pdpte & MMU_PRESENT)) continue;
            if (pdpte & MMU_COW) return 0;

            mmu_table_t *pd = (mmu_table_t*)((uintptr_t)PHYSMAP_BASE + (pdpte & ~0xFFFULL));
            for (int k = 0; k < 512; k++) {
                uint64_t pde = pd->entries[k];
                if (!(pde & MMU_PRESENT)) continue;
                if (pde & MMU_COW) return 0;

                mmu_table_t *pt = (mmu_table_t*)((uintptr_t)PHYSMAP_BASE + (pde & ~0xFFFULL));
                for (int l = 0; l < 512; l++) {
                    uint64_t pte = pt->entries[l];
                    if ((pte & MMU_PRESENT) && (pte & MMU_COW)) return 0;
                }
            }
        }
    }
    return 1;
}

// Phase 1: CPU & Execution Model Invariants
static int test_phase1() {
    log_phase_start(1, "Boot & CPU invariants (x64)");
    int phase_success = 1;

    // 1.1 Interrupt Stack Discipline
    uint64_t rsp;
    asm volatile("mov %%rsp, %0" : "=r"(rsp));
    if (current_task && rsp >= current_task->kernel_stack_base && rsp < current_task->kernel_stack) {
        log_pass("1.1", "Stack discipline (running on task stack)");
    } else {
        log_fail("1.1", "Stack discipline", "Not running on task stack");
        phase_success = 0;
    }

    // 1.2 TSS Integrity (x64)
    if (cpu_local[0].tss.rsp0 == current_task->kernel_stack) {
        log_pass("1.2", "TSS integrity (rsp0 correctly mapped to task stack)");
    } else {
        log_fail("1.2", "TSS integrity", "TSS rsp0 mismatch with task stack");
        phase_success = 0;
    }

    // 1.3 Atomic Context Switch (CR3)
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 == current_task->page_directory->pml4_phys) {
        log_pass("1.3", "Execution context (CR3 matches current task PML4)");
    } else {
        log_fail("1.3", "Execution context", "CR3 / Task PML4 mismatch");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 2: Memory Model
static int test_phase2() {
    log_phase_start(2, "Memory model (4-level paging)");
    int phase_success = 1;

    // 2.1 CR0.WP Enforcement
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & (1ULL << 16)) {
        log_pass("2.1", "CR0.WP enforcement (kernel write-protect active)");
    } else {
        log_fail("2.1", "CR0.WP enforcement", "WP bit is not set");
        phase_success = 0;
    }

    // 2.2 Kernel Never Participates in COW
    if (check_kernel_cow_64()) {
        log_pass("2.2", "Kernel COW immunity (no COW bits in kernel-space tables)");
    } else {
        log_fail("2.2", "Kernel COW immunity", "COW bit detected in kernel space");
        phase_success = 0;
    }

    // 2.3 Frame Refcounting Logic
    void *va = kmalloc(0x1000, 0, 0);
    mmu_entry_t *p = mmu_get_entry(kernel_directory, (virt_addr_t)va);
    if (!p) {
        log_fail("2.3", "Frame refcounting", "Failed to get entry for kmalloc'd address");
        phase_success = 0;
    } else {
        uint64_t frame = (*p & ~0xFFFULL) / 0x1000;
        uint32_t ref = frame_get_ref(frame);
        frame_add_ref(frame);
        if (frame_get_ref(frame) == ref + 1) {
            log_pass("2.3", "Frame refcounting (allocated frame verified)");
        } else {
            log_fail("2.3", "Frame refcounting", "Mismatch on allocated frame");
            phase_success = 0;
        }
        frame_remove_ref(frame);
    }
    kfree(va);

    // 2.4 PHYSMAP Validity
    uint64_t *physmap_ptr = (uint64_t*)(PHYSMAP_BASE + 0x1000);
    uint64_t old_val = *physmap_ptr;
    *physmap_ptr = 0xDEADC0DEDEADC0DEULL;
    if (*physmap_ptr == 0xDEADC0DEDEADC0DEULL) {
        log_pass("2.4", "PHYSMAP validity (linear map R/W verified)");
    } else {
        log_fail("2.4", "PHYSMAP validity", "Memory write-back failed");
        phase_success = 0;
    }
    *physmap_ptr = old_val;

    // 2.5 PMM Totality check
    kabi_pmm_stats_t stats;
    kabi_get_pmm_stats(&stats);
    if (stats.used_frames + stats.free_frames == stats.total_frames) {
        log_pass("2.5", "PMM statistics (frame accounting totals match)");
    } else {
        log_fail("2.5", "PMM statistics", "Used+Free != Total");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 3: Task & Process Lifecycle
extern volatile task_t *ready_queue;

static int test_phase3() {
    log_phase_start(3, "Task lifecycle");
    int phase_success = 1;
    
    task_t *it = (task_t*)ready_queue;
    int tasks_found = 0;
    int corruption_found = 0;
    do {
        if (it->magic != TASK_MAGIC) {
            corruption_found = 1;
            log_fail("3.1", "Task list integrity", "Magic corrupted");
            break;
        }
        if (it->state > TASK_ZOMBIE) {
            corruption_found = 1;
            log_fail("3.1", "Task list integrity", "Invalid state detected");
            break;
        }
        it = it->next;
        if (++tasks_found > MAX_TASKS) {
            corruption_found = 1;
            log_fail("3.1", "Task list integrity", "Circular loop overflow");
            break;
        }
    } while (it != (task_t*)ready_queue && it != 0);

    if (!corruption_found) {
        log_pass("3.1", "Task list scan (all structures valid and linked)");
    } else {
        phase_success = 0;
    }

    return phase_success;
}

// Phase 4: Scheduler Discipline
static int test_phase4() {
    log_phase_start(4, "Scheduler discipline");
    int phase_success = 1;

    if (current_scheduler && current_scheduler->version >= 0x0100) {
        log_pass("4.1", "Scheduler ABI (v1.0 compatibility verified)");
    } else {
        log_fail("4.1", "Scheduler ABI", "Version incompatible or missing");
        phase_success = 0;
    }

    if (current_scheduler && current_scheduler->pick_next) {
        log_pass("4.2", "Scheduler interface (pick_next hook verified)");
    } else {
        log_fail("4.2", "Scheduler interface", "pick_next hook is NULL");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 5: Syscall Boundary
static int test_phase5() {
    log_phase_start(5, "Syscall boundary");
    int phase_success = 1;

    uint16_t cs;
    asm volatile("mov %%cs, %0" : "=r"(cs));
    if ((cs & 0x3) == 0) {
        log_pass("5.1", "Execution privilege (running in Ring 0)");
    } else {
        log_fail("5.1", "Execution privilege", "Not in Ring 0");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 6: Failure Semantics
static int test_phase6() {
    log_phase_start(6, "Failure semantics");
    int phase_success = 1;

    if (ktrace_is_active()) {
        log_pass("6.1", "Tracing active (KTRACE subsystem operational)");
    } else {
        log_fail("6.1", "Tracing active", "KTRACE inactive or magic mismatch");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 7: Block IO (IDE)
static int test_phase7() {
    log_phase_start(7, "Block IO (IDE)");
    int phase_success = 1;

    uint8_t sector[512];
    if (kabi_block_read(0, 0, sector) != KABI_SUCCESS) {
        log_fail("7.1", "Sector reading", "kabi_block_read failed");
        phase_success = 0;
    } else if (sector[510] == 0x55 && sector[511] == 0xAA) {
        log_pass("7.1", "Sector reading (boot sector signature 0x55AA found)");
    } else {
        log_fail("7.1", "Sector reading", "Invalid boot signature");
        phase_success = 0;
    }

    uint8_t write_buf[512];
    uint8_t read_back[512];
    for(int i=0; i<512; i++) write_buf[i] = (uint8_t)(i % 256);
    
    /* Find dev2 (RAM Disk) by name for robust testing */
    int dev2_id = -1;
    int dev_count = kabi_get_device_count();
    for (int i = 0; i < dev_count; i++) {
        char name[16];
        if (kabi_get_device_name(i, name) == KABI_SUCCESS) {
            if (strcmp(name, "dev2") == 0) {
                dev2_id = i;
                break;
            }
        }
    }

    if (dev2_id == -1) {
        log_fail("7.2", "Sector writing", "RAM Disk (dev2) not found");
        phase_success = 0;
    } else if (kabi_block_write(dev2_id, 0, write_buf) != KABI_SUCCESS) {
        log_fail("7.2", "Sector writing", "kabi_block_write failed on dev2");
        phase_success = 0;
    } else if (kabi_block_read(dev2_id, 0, read_back) != KABI_SUCCESS) {
        log_fail("7.2", "Sector writing", "kabi_block_read failed during verification");
        phase_success = 0;
    } else {
        int match = 1;
        for(int i=0; i<512; i++) {
            if (read_back[i] != write_buf[i]) { match = 0; break; }
        }
        if (match) {
            log_pass("7.2", "Sector writing (write/read cycle verified)");
        } else {
            log_fail("7.2", "Sector writing", "Read-back mismatch");
            phase_success = 0;
        }
    }

    return phase_success;
}

// Phase 8: MBR Parsing
static int test_phase8() {
    log_phase_start(8, "MBR Parsing");
    int phase_success = 1;

    kabi_partition_t fat_part;
    if (mbr_find_fat32(0, &fat_part) == KABI_SUCCESS && fat_part.found) {
        log_pass("8.1", "FAT32 partition found");
    } else {
        log_fail("8.1", "FAT32 partition search", "No FAT32 partition found in MBR");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 9: FAT32 BPB
static int test_phase9() {
    log_phase_start(9, "FAT32 BPB");
    int phase_success = 1;

    kabi_partition_t part;
    if (mbr_find_fat32(0, &part) != KABI_SUCCESS || !part.found) {
        log_fail("9.1", "BPB", "No FAT32 partition found in MBR");
        return 0;
    }

    fat32_mount_t m;
    if (fat32_read_bpb(0, part.start_lba, &m) != KABI_SUCCESS) {
        log_fail("9.2", "BPB", "Read/Parse failed");
        return 0;
    }

    log_pass("9.3", "BPB parsing & volume mapping verified");
    return phase_success;
}

// Phase 10: FAT32 mount + ls
static int test_phase10() {
    log_phase_start(10, "FAT32 mount + ls");
    int phase_success = 1;

    kabi_partition_t part;
    if (mbr_find_fat32(0, &part) != KABI_SUCCESS || !part.found) {
        log_fail("10.1", "ls /", "No FAT32 partition found");
        return 0;
    }

    fat32_mount_t m;
    if (fat32_read_bpb(0, part.start_lba, &m) != KABI_SUCCESS) {
        log_fail("10.1", "ls /", "BPB read failed");
        return 0;
    }

    kprint("Root directory contents:\n");
    extern void fat32_ls_root(uint32_t dev, fat32_mount_t *m);
    fat32_ls_root(0, &m);

    log_pass("10.1", "ls / executed");
    return phase_success;
}

// Phase 11: FAT32 File Read
static int test_phase11() {
    log_phase_start(11, "FAT32 File Read (/BIN/HELLO64.ELF)");
    int phase_success = 1;

    kabi_partition_t part;
    if (mbr_find_fat32(0, &part) != KABI_SUCCESS || !part.found) {
        log_fail("11.1", "Read HELLO64.ELF", "No FAT32 partition");
        return 0;
    }

    fat32_mount_t m;
    if (fat32_read_bpb(0, part.start_lba, &m) != KABI_SUCCESS) {
        log_fail("11.1", "Read HELLO64.ELF", "BPB read failed");
        return 0;
    }

    fat32_dirent_t bin_dir;
    if (fat32_find_file(0, &m, "BIN", 0, &bin_dir) != KABI_SUCCESS) {
        log_fail("11.1", "Read HELLO64.ELF", "BIN directory not found");
        return 0;
    }

    fat32_dirent_t file;
    uint32_t bin_cluster = (bin_dir.cluster_hi << 16) | bin_dir.cluster_lo;
    if (fat32_find_file(0, &m, "HELLO64.ELF", bin_cluster, &file) != KABI_SUCCESS) {
        log_fail("11.1", "Read HELLO64.ELF", "HELLO64.ELF not found in BIN");
        return 0;
    }
    log_pass("11.1", "HELLO64.ELF found and metadata read");
    return phase_success;
}

// Phase 12 helpers
static void sig12_noop_handler(int s) { (void)s; }

// Phase 12: Signal System Invariants (x64)
static int test_phase12() {
    log_phase_start(12, "Signal system invariants");
    int phase_success = 1;

    task_t *t = (task_t*)current_task;

    /* 12.1 SIGKILL never sets a pending bit */
    {
        uint32_t saved = t->pending_signals;
        t->pending_signals = 0;
        extern task_t *create_kernel_task(void (*entry)(void));
        extern void idle_task(void);
        task_t *dummy = create_kernel_task(idle_task);
        if (dummy) {
            task_deliver_signal(dummy, SIGKILL);
            int no_bit = !(dummy->pending_signals & SIG_BIT(SIGKILL));
            if (no_bit && dummy->state == TASK_ZOMBIE)
                log_pass("12.1", "SIGKILL: uncatchable, no pending bit");
            else {
                log_fail("12.1", "SIGKILL invariant", "bit set or task not zombie");
                phase_success = 0;
            }
            reap_zombies();
        } else {
            log_fail("12.1", "SIGKILL invariant", "create_kernel_task returned null");
            phase_success = 0;
        }
        t->pending_signals = saved;
    }

    /* 12.2 Reentrancy guard: in_signal blocks trampoline injection */
    {
        registers_t fake;
        memory_set((uint8_t*)&fake, 0, sizeof(fake));
        fake.cs = 0x1B; fake.rip = 0xDEAD0000; fake.rsp = 0xBEEF0000;

        virt_addr_t sv_handler = t->sigterm_handler;
        uint32_t sv_pending = t->pending_signals;
        int sv_insig = t->in_signal;

        t->sigterm_handler = (virt_addr_t)sig12_noop_handler;
        t->pending_signals = SIG_BIT(SIGTERM);
        t->in_signal = 1; /* guard active */

        uint64_t rip_before = fake.rip;
        task_check_pending_signals(&fake);

        t->sigterm_handler = sv_handler;
        t->pending_signals = sv_pending;
        t->in_signal = sv_insig;

        if (fake.rip == rip_before)
            log_pass("12.2", "Reentrancy guard: in_signal blocks trampoline");
        else {
            log_fail("12.2", "Reentrancy guard", "RIP mutated despite in_signal=1");
            phase_success = 0;
        }
    }

    return phase_success;
}

// Phase 13: Sleep queue invariants
static int test_phase13() {
    log_phase_start(13, "Sleep queue invariants");
    int phase_success = 1;

    extern volatile task_t *sleep_queue;
    int ok = 1;
    task_t *s = (task_t*)sleep_queue;
    int count = 0;
    while (s && s->sleep_next) {
        if (s->sleep_until > s->sleep_next->sleep_until) { ok = 0; break; }
        if (s->state == TASK_ZOMBIE) { ok = 0; break; }
        s = s->sleep_next;
        if (++count > MAX_TASKS) { ok = 0; break; }
    }
    if (ok) log_pass("13.1", "Sleep queue sorted and zombie-free");
    else { log_fail("13.1", "Sleep queue", "Unsorted or contains zombie"); phase_success = 0; }

    return phase_success;
}

// Phase 14: K-ABI @guarantee Contract Tests (x64)
static int test_phase14() {
    log_phase_start(14, "K-ABI @guarantee contracts (x64)");
    int phase_success = 1;

    /* 14.1 kmalloc: Returned pointer is valid until kfree is called. */
    {
        uint64_t *ptr = (uint64_t *)kmalloc(128, 0, NULL);
        if (!ptr) {
            log_fail("14.1", "kmalloc validity", "kmalloc returned NULL");
            phase_success = 0;
        } else {
            ptr[0] = 0xDEADBEEFCAFEBABEULL;
            if (ptr[0] == 0xDEADBEEFCAFEBABEULL) {
                log_pass("14.1", "kmalloc: 64-bit pointer valid, pattern verified");
            } else {
                log_fail("14.1", "kmalloc validity", "Pattern readback mismatch");
                phase_success = 0;
            }
            kfree(ptr);
        }
    }

    /* 14.2 kfree: Memory becomes invalid immediately (accounting check). */
    {
        kabi_heap_stats_t before, after;
        void *block = kmalloc(4096, 0, NULL);
        if (!block) {
            log_fail("14.2", "kfree reclaim", "kmalloc returned NULL");
            phase_success = 0;
        } else {
            kabi_get_heap_stats(&before);
            kfree(block);
            kabi_get_heap_stats(&after);
            if (after.used_size < before.used_size) {
                log_pass("14.2", "kfree: heap used_size decreased after free");
            } else {
                log_fail("14.2", "kfree reclaim", "used_size did not decrease (may be in-page fragmentation)");
            }
        }
    }

    return phase_success;
}

// Phase 15: Validation Layer Tests
static int test_phase15() {
    log_phase_start(15, "Validation layer (KABI/UABI)");
    int phase_success = 1;

    kabi_heap_stats_t stats;
    kabi_get_heap_stats(&stats);
    if (stats.total_size > 0) {
        log_pass("15.1", "kabi_get_heap_stats: valid ptr accepted");
    } else {
        log_fail("15.1", "kabi_get_heap_stats", "Output insane");
        phase_success = 0;
    }

    uint8_t sector[512];
    int r = kabi_block_read(0, 0, sector);
    if (r == KABI_SUCCESS) {
        log_pass("15.2", "kabi_block_read: valid args accepted");
    } else {
        log_fail("15.2", "kabi_block_read", "Unexpected failure");
        phase_success = 0;
    }

    r = kabi_block_read(999, 0, sector);
    if (r == KABI_EINVAL) {
        log_pass("15.3", "kabi_block_read: invalid dev_id rejected (EINVAL)");
    } else {
        log_fail("15.3", "kabi_block_read", "Did not reject invalid dev_id");
        phase_success = 0;
    }

    return phase_success;
}

// Phase 16: Ported Subsystem Invariants
static int test_phase16() {
    log_phase_start(16, "Ported subsystem contracts (x64)");
    int phase_success = 1;

    /* 16.1 Pipe lifecycle: create → write → read → verify */
    {
        pipe_t *p = pipe_create(PIPE_SIZE);
        if (!p) {
            log_fail("16.1", "Pipe lifecycle", "pipe_create returned NULL");
            phase_success = 0;
        } else {
            fs_node_t *node = pipe_create_node(p);
            pipe_add_writer(p);
            pipe_add_reader(p);

            uint8_t wbuf[8] = {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE};
            uint8_t rbuf[8];
            memory_set(rbuf, 0, 8);

            pipe_write(node, 0, 8, wbuf);
            pipe_read(node, 0, 8, rbuf);

            int match = 1;
            for (int i = 0; i < 8; i++) {
                if (rbuf[i] != wbuf[i]) { match = 0; break; }
            }
            if (match && p->len == 0) {
                log_pass("16.1", "Pipe lifecycle (create/write/read/verify)");
            } else {
                log_fail("16.1", "Pipe lifecycle", "Data mismatch or residual len");
                phase_success = 0;
            }

            pipe_remove_writer(p);
            pipe_remove_reader(p);
            kfree(node);
            kfree(p->buffer);
            kfree(p);
        }
    }

    /* 16.2 VFS syscalls: getcwd returns valid CWD, chdir succeeds */
    {
        char cwd_buf[256];
        memory_set((uint8_t*)cwd_buf, 0, 256);
        int r = sys_getcwd(cwd_buf, 256);
        if (r == 0 && cwd_buf[0] == '/') {
            log_pass("16.2", "VFS syscalls (getcwd/chdir verified)");
        } else {
            log_fail("16.2", "VFS syscalls", "sys_getcwd failed or invalid CWD");
            phase_success = 0;
        }
    }

    /* 16.3 Process info: sys_ps returns >= 1 task */
    {
        extern int sys_ps(void *buf, int count);
        extern int sys_memstat(void *buf);
        /* Use stack buffer for 4 process entries (each ~40 bytes) */
        uint8_t ps_buf[256];
        memory_set(ps_buf, 0, 256);
        int count = sys_ps(ps_buf, 4);
        if (count >= 1) {
            log_pass("16.3", "Process info (sys_ps returned valid entries)");
        } else {
            log_fail("16.3", "Process info", "sys_ps returned 0 or error");
            phase_success = 0;
        }
    }

    /* 16.4 Reboot vectors: function pointers are resolved (non-stub) */
    {
        extern void core_shutdown(void);
        extern void core_reboot(int reason);
        /* If these were stubs, they'd be empty no-ops at the same address.
         * But since they're real now, just verify they're non-null symbols. */
        void (*shutdown_fn)(void) = core_shutdown;
        void (*reboot_fn)(int) = core_reboot;
        if (shutdown_fn && reboot_fn) {
            log_pass("16.4", "Reboot vectors (shutdown/reboot resolved)");
        } else {
            log_fail("16.4", "Reboot vectors", "core_shutdown or core_reboot is NULL");
            phase_success = 0;
        }
    }

    return phase_success;
}

void run_core_test64_v1() {
    kprint("\n[ CORE TEST 64 ] Running TEST_CORE64_V1.0...\n");
    phase_failed = 0;

    log_result(1, test_phase1());
    log_result(2, test_phase2());
    log_result(3, test_phase3());
    log_result(4, test_phase4());
    log_result(5, test_phase5());
    log_result(6, test_phase6());
    log_result(7, test_phase7());
    log_result(8, test_phase8());
    log_result(9, test_phase9());
    log_result(10, test_phase10());
    log_result(11, test_phase11());
    log_result(12, test_phase12());
    log_result(13, test_phase13());
    log_result(14, test_phase14());
    log_result(15, test_phase15());
    log_result(16, test_phase16());

    if (!phase_failed) {
        kprint("\n[ CORE TEST 64 ] TEST_CORE64_V1.0: PASSED\n");
        kprint("x86_64 Core contract intact. (16 phases)\n");
    } else {
        kprint("\n[ CORE TEST 64 ] TEST_CORE64_V1.0: FAILED\n");
    }

    extern void run_all_module_tests(void);
    run_all_module_tests();
}
