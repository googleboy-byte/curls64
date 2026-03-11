#define KABI_DEBUG 1
#include "../../include/kabi/kabi_v1.h"
#include "kernel_api.h"
#include "task.h"
#include "../cpu/paging.h"
#include "tests/core_tests/core_test64_v1.h"
#include "tests/unit/test_paging.h"
#include "tests/stress/test_stress.h"
#include "../../libc/mem.h"
#include "../cpu/isr.h"
#include "../modules/drivers/shutdown.h"
#include "../modules/drivers/screen.h"
#include "../modules/drivers/keyboard.h"
#include "../modules/drivers/ide.h"
#include "../cpu/timer.h"
#include "../../libc/string.h"

#include "block_dev.h"
#include "signal.h"
#include "abi_validate.h"

static int kabi_debug_mode = 0;

/* --- Demo RAM Disk Implementation --- */
static uint8_t demo_ramdisk[2 * 512]; // 2 sectors

static int ramdisk_read(uint64_t lba, uint8_t *buf) {
    if (lba >= 2) return -1;
    memory_copy(demo_ramdisk + (lba * 512), buf, 512);
    return 0;
}

static int ramdisk_write(uint64_t lba, uint8_t *buf) {
    if (lba >= 2) return -1;
    memory_copy(buf, demo_ramdisk + (lba * 512), 512);
    return 0;
}

void kabi_bridge_init() {
    kprint("    - Initializing block devices...\n");
    block_dev_init();

    // Register dev0: IDE Primary
    kprint("    - Registering IDE dev0...\n");
    kabi_block_device_t ide;
    memory_set((uint8_t*)&ide, 0, sizeof(ide));
    strcpy(ide.name, "dev0");
    ide.read_sector = ide_read_sector;
    ide.write_sector = ide_write_sector;
    ide.size = 0; // Unknown/dynamic
    block_dev_register(ide);

    // Register dev2: RAM Disk
    kprint("    - Registering RAM dev2...\n");
    kabi_block_device_t ram;
    memory_set((uint8_t*)&ram, 0, sizeof(ram));
    strcpy(ram.name, "dev2");
    ram.read_sector = ramdisk_read;
    ram.write_sector = ramdisk_write;
    ram.size = 2;
    block_dev_register(ram);
    kprint("    - Block devices registered.\n");

    // Fill ramdisk with some data for testing
    memory_set(demo_ramdisk, 'A', 512);
    memory_set(demo_ramdisk + 512, 'B', 512);
}

/* ═══════════════════════════════════════════════════════════════
 *  Memory Management
 * ═══════════════════════════════════════════════════════════════ */

void kabi_get_heap_stats(kabi_heap_stats_t *stats) {
    KABI_VALIDATE_PTR(stats, "kabi_get_heap_stats");
    get_heap_stats((heap_stats_t*)stats);
}

void kabi_get_pmm_stats(kabi_pmm_stats_t *stats) {
    KABI_VALIDATE_PTR(stats, "kabi_get_pmm_stats");
    get_pmm_stats((pmm_stats_t*)stats);
}

int kabi_map_user_memory(virt_addr_t addr, size_t len, uint32_t flags) {
    (void)flags; // Currently unused semantic flag
    KABI_VALIDATE_NONZERO(len, "kabi_map_user_memory");
    KABI_VALIDATE_PTR(current_task, "kabi_map_user_memory");
    KABI_VALIDATE_PTR(current_task->page_directory, "kabi_map_user_memory");
    // Policy: CORE decides how to map. Bridging to promote_to_user_table.
    promote_to_user_table((page_directory_t*)current_task->page_directory, addr, len);
    return KABI_SUCCESS;
}

void kabi_clear_screen() {
    clear_screen();
}

extern void core_shutdown();
extern void core_reboot(reboot_reason_t reason);

int kabi_request_shutdown() {
    KABI_VALIDATE_PTR(current_task, "kabi_request_shutdown");
    if (!(current_task->capabilities & CAP_SHUTDOWN)) {
        kprint("[VALIDATE FAIL] kabi_request_shutdown: task lacks CAP_SHUTDOWN\n");
        return KABI_EPERM;
    }
    core_shutdown();
    // This function is noreturn if successful.
    return KABI_SUCCESS;
}

int kabi_request_reboot(reboot_reason_t reason) {
    KABI_VALIDATE_PTR(current_task, "kabi_request_reboot");
    KABI_VALIDATE_RANGE(reason, REBOOT_REASON_ADMIN, REBOOT_REASON_UPDATE, "kabi_request_reboot");
    if (!(current_task->capabilities & CAP_REBOOT)) {
        kprint("[VALIDATE FAIL] kabi_request_reboot: task lacks CAP_REBOOT\n");
        return KABI_EPERM;
    }
    core_reboot(reason);
    // This function is noreturn if successful.
    return KABI_SUCCESS;
}

/* ═══════════════════════════════════════════════════════════════
 *  Task Management
 * ═══════════════════════════════════════════════════════════════ */

void kabi_ps() {
    ps();
}

int kabi_spawn_process(virt_addr_t entry_point, virt_addr_t user_stack) {
    KABI_VALIDATE_NONZERO(entry_point, "kabi_spawn_process");
    KABI_VALIDATE_NONZERO(user_stack, "kabi_spawn_process");
    int pid = spawn_process(entry_point, user_stack);
    if (pid < 0) return KABI_ENOMEM;
    return pid;
}

int kabi_task_signal(int pid, kabi_signal_t signal) {
    KABI_VALIDATE_RANGE(pid, 1, 65535, "kabi_task_signal");
    KABI_VALIDATE_RANGE(signal, 1, 31, "kabi_task_signal");
    return task_send_signal(pid, (int)signal);
}

int kabi_sigaction(int sig, virt_addr_t handler_eip) {
    /* Only SIGTERM and SIGINT can have handlers */
    if (sig != KABI_SIGTERM && sig != KABI_SIGINT) {
        kprint("[VALIDATE FAIL] kabi_sigaction: invalid signal ");
        char buf[8]; int_to_ascii(sig, buf); kprint(buf);
        kprint(" (only SIGTERM/SIGINT allowed)\n");
        return KABI_EINVAL;
    }
    KABI_VALIDATE_PTR(current_task, "kabi_sigaction");
    current_task->sigterm_handler = handler_eip;
    return KABI_SUCCESS;
}

void kabi_wait_for_children() {
    wait_for_children();
}

void kabi_kill_all_children() {
    task_send_sigint_foreground();
}

extern void first_user_entry_trampoline(virt_addr_t entry, virt_addr_t stack) __attribute__((noreturn));
__attribute__((noreturn)) void kabi_jump_to_user_mode(virt_addr_t entry, virt_addr_t stack) {
    KABI_VALIDATE_NONZERO(entry, "kabi_jump_to_user_mode");
    KABI_VALIDATE_NONZERO(stack, "kabi_jump_to_user_mode");
    KABI_VALIDATE_ALIGNED(stack, 4, "kabi_jump_to_user_mode");
    first_user_entry_trampoline(entry, stack);
}

__attribute__((noreturn)) void kabi_task_exit() {
    /* Use kill(getpid()) to ensure SIGCHLD is sent to parent and cleanup is triggered */
    kill(getpid());
    while(1); // Should never reach here
}

int kabi_sys_exec(const char *path) {
    KABI_VALIDATE_PTR(path, "kabi_sys_exec");
    (void)path;
    return -1;
}

int kabi_sys_execve(const char *path, char **argv) {
    KABI_VALIDATE_PTR(path, "kabi_sys_execve");
    (void)argv;
    return -1; // Shell will use int 0x80 directly
}

void kabi_yield() {
    // Current scheduler yield is just an interrupt
    asm volatile("int $0x20");
}

int kabi_fork() {
    return fork();
}

void kabi_kill(int pid) {
    KABI_VALIDATE_RANGE(pid, 1, 65535, "kabi_kill");
    kill(pid);
}

/* ═══════════════════════════════════════════════════════════════
 *  Process Enumeration (K-ABI v2)
 * ═══════════════════════════════════════════════════════════════ */

extern volatile task_t *ready_queue;

int kabi_task_iter_begin(kabi_task_iter_t *it) {
    KABI_VALIDATE_PTR(it, "kabi_task_iter_begin");
    if (!ready_queue) return KABI_ENOENT;
    
    it->current = (void*)ready_queue;
    it->start = (void*)ready_queue;
    it->first = 1;
    return KABI_SUCCESS;
}

int kabi_task_next(kabi_task_iter_t *it, kabi_task_info_t *info) {
    KABI_VALIDATE_PTR(it, "kabi_task_next");
    KABI_VALIDATE_PTR(info, "kabi_task_next");
    if (!it->current) return 0;
    
    task_t *t = (task_t*)it->current;
    
    // Check if we looped back (and it's not the first iteration)
    if (!it->first && t == (task_t*)it->start) {
        return 0; 
    }
    
    // Copy info
    info->id = t->id;
    info->user_esp = t->user_esp;
    info->user_eip = t->user_eip;
    info->kernel_stack = t->kernel_stack;
    info->capabilities = t->capabilities;
    info->state = t->state;
    info->parent_id = t->parent ? t->parent->id : 0;
    info->ticks = t->ticks;
    
    // Advance
    it->current = t->next;
    it->first = 0;
    
    // Safety check for broken links
    if (!it->current) return 0;
    
    return 1;
}

/* ═══════════════════════════════════════════════════════════════
 *  Hardware Support
 * ═══════════════════════════════════════════════════════════════ */

void kabi_int_to_ascii(int n, char str[]) {
    KABI_VALIDATE_PTR(str, "kabi_int_to_ascii");
    int_to_ascii(n, str);
}

void kabi_hex_to_ascii(uint64_t n, char str[]) {
    KABI_VALIDATE_PTR(str, "kabi_hex_to_ascii");
    hex_to_ascii(n, str);
}

void kabi_irq_register(uint8_t n, kabi_irq_handler_t handler) {
    KABI_VALIDATE_PTR((void*)handler, "kabi_irq_register");
    KABI_VALIDATE_RANGE(n, 0, 15, "kabi_irq_register");
    register_interrupt_handler(IRQ0 + n, (isr_t)handler);
}

void kabi_get_line(char *buf) {
    KABI_VALIDATE_PTR(buf, "kabi_get_line");
    get_line(buf);
}

uint64_t kabi_get_ticks() {
    return get_ticks();
}

int kabi_block_read(uint32_t dev_id, uint64_t lba, uint8_t *buf) {
    KABI_VALIDATE_PTR(buf, "kabi_block_read");
    kabi_block_device_t *dev = block_dev_get_by_index(dev_id);
    if (!dev) {
        kprint("[VALIDATE FAIL] kabi_block_read: invalid dev_id ");
        char s[8]; int_to_ascii(dev_id, s); kprint(s); kprint("\n");
        return KABI_EINVAL;
    }
    if (!dev->read_sector) {
        kprint("[VALIDATE FAIL] kabi_block_read: dev has no read_sector\n");
        panic("[VALIDATE] KABI contract violated");
    }
    
    uint64_t target_lba = lba;
    if (dev->is_partition) {
        target_lba += dev->start_lba;
    }

    int result = (dev->read_sector(target_lba, buf) == 0) ? KABI_SUCCESS : KABI_EIO;
    return result;
}

int kabi_block_write(uint32_t dev_id, uint64_t lba, uint8_t *buf) {
    KABI_VALIDATE_PTR(buf, "kabi_block_write");
    kabi_block_device_t *dev = block_dev_get_by_index(dev_id);
    if (!dev) {
        kprint("[VALIDATE FAIL] kabi_block_write: invalid dev_id ");
        char s[8]; int_to_ascii(dev_id, s); kprint(s); kprint("\n");
        return KABI_EINVAL;
    }
    if (!dev->write_sector) {
        kprint("[VALIDATE FAIL] kabi_block_write: dev has no write_sector\n");
        panic("[VALIDATE] KABI contract violated");
    }

    uint64_t target_lba = lba;
    if (dev->is_partition) {
        target_lba += dev->start_lba;
    }

    int result = (dev->write_sector(target_lba, buf) == 0) ? KABI_SUCCESS : KABI_EIO;
    return result;
}

int kabi_get_device_count() {
    return block_dev_get_count();
}

int kabi_get_device_name(int index, char *buf) {
    KABI_VALIDATE_PTR(buf, "kabi_get_device_name");
    kabi_block_device_t *dev = block_dev_get_by_index(index);
    if (!dev) {
        kprint("[VALIDATE FAIL] kabi_get_device_name: invalid index ");
        char s[8]; int_to_ascii(index, s); kprint(s); kprint("\n");
        return KABI_EINVAL;
    }
    strcpy(buf, dev->name);
    return KABI_SUCCESS;
}

uint64_t kabi_get_device_size(int index) {
    kabi_block_device_t *dev = block_dev_get_by_index(index);
    if (!dev) return 0;
    return dev->size;
}

/* ═══════════════════════════════════════════════════════════════
 *  Debug & Testing
 * ═══════════════════════════════════════════════════════════════ */

#ifdef KABI_DEBUG
void kabi_debug_run_test(uint32_t test_id, const char *args) {
    switch (test_id) {
        case KABI_DEBUG_TEST_PAGING:
            if (!args || args[0] == '\0') {
                print_test_menu();
            } else {
                handle_test_command((char*)args);
            }
            break;
        case KABI_DEBUG_TEST_STRESS:
            if (!args || args[0] == '\0') {
                print_stress_menu();
            } else {
                handle_stress_command((char*)args);
            }
            break;
        case KABI_DEBUG_TEST_CORE:
            run_core_test64_v1();
            break;
        default:
            kprint("[VALIDATE FAIL] kabi_debug_run_test: unknown test_id ");
            char s[8]; int_to_ascii(test_id, s); kprint(s); kprint("\n");
            break;
    }
}
#endif

int kabi_debug_enabled() {
    return kabi_debug_mode;
}

void kabi_set_debug(int enabled) {
    kabi_debug_mode = enabled;
}
