#include "../../../include/module/module_abi_v1.h"
#include "sysmon_top.h"

/**
 * System Monitor (top) Module
 * 
 * This module uses only the K-ABI to provide system monitoring functionality.
 * It demonstrates how to build a "top"-like utility purely through the stable
 * kernel ABI without accessing internal kernel structures directly.
 */

// Module state
static uint64_t last_update_tick = 0;
static uint64_t total_tasks = 0;

/**
 * State name helper
 */
static const char* get_state_name(uint8_t state) {
    // K-ABI officially exposes these constants now.
    switch (state) {
        case KABI_TASK_READY:   return "READY ";
        case KABI_TASK_RUNNING: return "RUN   ";
        case KABI_TASK_WAITING: return "WAIT  ";
        case KABI_TASK_ZOMBIE:  return "ZOMBIE";
        default: return "UNKNWN";
    }
}

/**
 * Count total tasks in the system using iterator
 */
static uint64_t count_tasks(void) {
    kabi_task_iter_t it;
    if (kabi_task_iter_begin(&it) != KABI_SUCCESS) return 0;
    
    kabi_task_info_t info;
    uint64_t count = 0;
    
    while (kabi_task_next(&it, &info)) {
        count++;
    }
    
    return count;
}

/**
 * Display header with system statistics
 */
static void display_header(void) {
    kabi_heap_stats_t heap;
    kabi_pmm_stats_t pmm;
    uint64_t ticks = kabi_get_ticks();
    
    kabi_get_heap_stats(&heap);
    kabi_get_pmm_stats(&pmm);
    
    total_tasks = count_tasks();
    
    // Display system uptime
    char tick_str[16];
    kabi_int_to_ascii(ticks, tick_str);
    kprint("=== SYSTEM MONITOR (top) ===\n");
    kprint("Uptime: ");
    kprint(tick_str);
    kprint(" ticks\n\n");
    
    // Memory statistics
    kprint("--- Memory Usage ---\n");
    
    char str[16];
    kabi_int_to_ascii(heap.total_size / 1024, str);
    kprint("Heap Total:  ");
    kprint(str);
    kprint(" KB\n");
    
    kabi_int_to_ascii(heap.used_size / 1024, str);
    kprint("Heap Used:   ");
    kprint(str);
    kprint(" KB\n");
    
    kabi_int_to_ascii(heap.free_size / 1024, str);
    kprint("Heap Free:   ");
    kprint(str);
    kprint(" KB\n");
    
    kabi_int_to_ascii((pmm.total_frames * 4096) / 1024, str);
    kprint("Phys Total:  ");
    kprint(str);
    kprint(" KB\n");
    
    kabi_int_to_ascii((pmm.used_frames * 4096) / 1024, str);
    kprint("Phys Used:   ");
    kprint(str);
    kprint(" KB\n");
    
    kabi_int_to_ascii((pmm.free_frames * 4096) / 1024, str);
    kprint("Phys Free:   ");
    kprint(str);
    kprint(" KB\n\n");
}

/**
 * Helper to print a string with trailing spaces for alignment
 */
static void print_padded(const char* str, int width) {
    int len = 0;
    while (str[len]) len++;
    
    kprint(str);
    for (int i = len; i < width; i++) {
        kprint(" ");
    }
}

/**
 * Helper to print hex with padding
 */
static void hex_padded(uint64_t val, int width) {
    char str[32];
    kabi_hex_to_ascii(val, str);
    print_padded(str, width);
}

/**
 * Display process table header
 */
static void display_process_header(void) {
    kprint("--- Process Table ---\n");
    kprint("PID   STATE   ESP        EIP        CAPS   PARENT\n");
    kprint("----- ------- ---------- ---------- ------ ------\n");
}

/**
 * Display a single process entry
 */
static void display_process(kabi_task_info_t *task) {
    char str[16];
    
    // PID
    kabi_int_to_ascii(task->id, str);
    print_padded(str, 6);
    
    // State
    print_padded(get_state_name(task->state), 8);
    
    // ESP
    hex_padded(task->user_esp, 11);
    
    // EIP
    hex_padded(task->user_eip, 11);
    
    // Capabilities
    hex_padded(task->capabilities, 7);
    
    // Parent PID
    if (task->parent_id) {
        kabi_int_to_ascii(task->parent_id, str);
        kprint(str);
    } else {
        kprint("NONE");
    }
    
    kprint("\n");
}

/**
 * Main top display function
 */
void sysmon_top(void) {
    // Update timestamp
    last_update_tick = kabi_get_ticks();
    
    // Display header with stats
    display_header();
    
    // Display process table
    display_process_header();
    
    kabi_task_iter_t it;
    if (kabi_task_iter_begin(&it) != KABI_SUCCESS) {
        kprint("No tasks accessible.\n");
        return;
    }
    
    kabi_task_info_t info;
    uint32_t count = 0;
    
    while (kabi_task_next(&it, &info)) {
        display_process(&info);
        count++;
        // Safety Break? Not needed with iterator pattern but good for sanity
        if (count > 1024) break;
    }
    
    kprint("\n");
    char str[16];
    kabi_int_to_ascii(count, str);
    kprint("Total processes (visible): ");
    kprint(str);
    kprint("\n");
}

/**
 * Display verbose process information
 */
void sysmon_ps_verbose(void) {
    kprint("=== PROCESS LIST (verbose) ===\n\n");
    
    kabi_task_iter_t it;
    if (kabi_task_iter_begin(&it) != KABI_SUCCESS) {
        kprint("No tasks accessible.\n");
        return;
    }
    
    kabi_task_info_t task;
    
    while (kabi_task_next(&it, &task)) {
        char str[16];
        
        kprint("--- Process ");
        kabi_int_to_ascii(task.id, str);
        kprint(str);
        kprint(" ---\n");
        
        kprint("  State:       ");
        kprint((char*)get_state_name(task.state));
        kprint("\n");
        
        kprint("  User ESP:    0x");
        kabi_hex_to_ascii(task.user_esp, str);
        kprint(str);
        kprint("\n");
        
        kprint("  User EIP:    0x");
        kabi_hex_to_ascii(task.user_eip, str);
        kprint(str);
        kprint("\n");
        
        kprint("  Kernel Stack: 0x");
        kabi_hex_to_ascii(task.kernel_stack, str);
        kprint(str);
        kprint("\n");
        
        kprint("  Capabilities: 0x");
        kabi_hex_to_ascii(task.capabilities, str);
        kprint(str);
        kprint("\n");
        
        if (task.parent_id) {
            kprint("  Parent PID:  ");
            kabi_int_to_ascii(task.parent_id, str);
            kprint(str);
            kprint("\n");
        }
        
        kprint("\n");
    }
}

/**
 * Display only memory statistics
 */
void sysmon_mem_stats(void) {
    kabi_heap_stats_t heap;
    kabi_pmm_stats_t pmm;
    char str[16];
    
    kabi_get_heap_stats(&heap);
    kabi_get_pmm_stats(&pmm);
    
    kprint("=== MEMORY STATISTICS ===\n\n");
    
    kprint("--- Kernel Heap ---\n");
    kabi_int_to_ascii(heap.total_size, str);
    kprint("Total:      ");
    kprint(str);
    kprint(" bytes (");
    kabi_int_to_ascii(heap.total_size / 1024, str);
    kprint(str);
    kprint(" KB)\n");
    
    kabi_int_to_ascii(heap.used_size, str);
    kprint("Used:       ");
    kprint(str);
    kprint(" bytes (");
    kabi_int_to_ascii(heap.used_size / 1024, str);
    kprint(str);
    kprint(" KB)\n");
    
    kabi_int_to_ascii(heap.free_size, str);
    kprint("Free:       ");
    kprint(str);
    kprint(" bytes (");
    kabi_int_to_ascii(heap.free_size / 1024, str);
    kprint(str);
    kprint(" KB)\n");
    
    kabi_hex_to_ascii(heap.max_addr, str);
    kprint("Max Addr:   0x");
    kprint(str);
    kprint("\n\n");
    
    kprint("--- Physical Memory ---\n");
    kabi_int_to_ascii(pmm.total_frames, str);
    kprint("Total Frames: ");
    kprint(str);
    kprint(" (");
    kabi_int_to_ascii((pmm.total_frames * 4096) / 1024, str);
    kprint(str);
    kprint(" KB)\n");
    
    kabi_int_to_ascii(pmm.used_frames, str);
    kprint("Used Frames:  ");
    kprint(str);
    kprint(" (");
    kabi_int_to_ascii((pmm.used_frames * 4096) / 1024, str);
    kprint(str);
    kprint(" KB)\n");
    
    kabi_int_to_ascii(pmm.free_frames, str);
    kprint("Free Frames:  ");
    kprint(str);
    kprint(" (");
    kabi_int_to_ascii((pmm.free_frames * 4096) / 1024, str);
    kprint(str);
    kprint(" KB)\n");
    
    // Calculate percentage
    if (pmm.total_frames > 0) {
        uint32_t percent = (pmm.used_frames * 100) / pmm.total_frames;
        kabi_int_to_ascii(percent, str);
        kprint("Usage:        ");
        kprint(str);
        kprint("%\n");
    }
}

/**
 * Module initialization function
 * Called during kernel boot to register the module
 */
void sysmon_init(void) {
    kprint("[SYSMON] System Monitor module initialized\n");
    kprint("[SYSMON] NOTE: Now using K-ABI Task Iterator (Safe Mode)\n");
    kprint("[SYSMON] Available commands:\n");
    kprint("[SYSMON]   - sysmon_top()       : Display system overview\n");
    kprint("[SYSMON]   - sysmon_ps_verbose(): Detailed process info\n");
    kprint("[SYSMON]   - sysmon_mem_stats() : Memory statistics\n");
}

/* --- Module Registration --- */
static int sysmon_module_init(void) { sysmon_init(); return 0; }

kabi_module_t __kabi_module_sysmon = {
    .name           = "sysmon",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = sysmon_module_init,
    .exit           = NULL,
    .description    = "System monitor (top/ps/mem)"
};
