#ifndef KABI_V1_H
#define KABI_V1_H

/**
 * Curls Kernel K-ABI v1: The Sacred Layer Surface
 * 
 * VERSION: 0x0001
 * 
 * DESIGN PRINCIPLE:
 * 1. The CORE owns invariants; Modules own policy.
 * 2. ABI exposes semantic operations, never internal mechanisms.
 * 3. Error codes are negative; Success is non-negative.
 * 4. Pointers returned by ABI are opaque handles unless specified.
 */

#include <stdint.h>
#include <stddef.h>
#include <kernel/arch_types.h>

#define KABI_VERSION 0x0001

/* --- Forward Declarations & Opaque Types --- */
typedef void kabi_task_t;
typedef void kabi_fs_node_t;
typedef void kabi_page_directory_t;

typedef struct {
    uint32_t total_size;
    uint32_t used_size;
    uint32_t free_size;
    uint32_t max_addr;
} kabi_heap_stats_t;

typedef struct {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t free_frames;
} kabi_pmm_stats_t;

/* --- Common Error Codes --- */
#define KABI_SUCCESS  0
#define KABI_EPERM   (-1)  /* Operation not permitted */
#define KABI_EINVAL  (-2)  /* Invalid argument */
#define KABI_ENOMEM  (-3)  /* Out of memory */
#define KABI_ENOENT  (-4)  /* No such file or directory */
#define KABI_EIO     (-5)  /* Input/output error */

/* --- Capabilities --- */
#define CAP_NONE          0
#define CAP_REBOOT        (1 << 0) /* Ability to trigger a system reset */
#define CAP_SHUTDOWN      (1 << 1) /* Ability to power off the machine */
#define CAP_SYS_ADMIN     (1 << 2) /* General administrative override */

typedef enum {
    REBOOT_REASON_ADMIN,
    REBOOT_REASON_PANIC,
    REBOOT_REASON_UPDATE
} reboot_reason_t;

/* --- Memory Management --- */

/**
 * @brief Allocate memory from the kernel heap.
 * @return Semantic: A pointer to at least 'size' bytes of kernel-accessible memory.
 * @guarantee Returned pointer is valid until kfree is called.
 */
void *kmalloc(size_t size, int align, phys_addr_t *phys);

/**
 * @brief Free a kernel memory allocation.
 * @guarantee The memory at 'p' becomes invalid immediately.
 */
void kfree(void *p);

/**
 * @brief Get kernel heap statistics.
 */
void kabi_get_heap_stats(kabi_heap_stats_t *stats);

/**
 * @brief Get physical memory manager statistics.
 */
void kabi_get_pmm_stats(kabi_pmm_stats_t *stats);

/**
 * @brief Map a range of memory as accessible to user-mode tasks.
 * @param addr Virtual start address.
 * @param len Length in bytes.
 * @param flags Reserved for future permissions (read/write/exec).
 * @return KABI_SUCCESS on success, negative error code otherwise.
 */
int kabi_map_user_memory(virt_addr_t addr, uint32_t len, uint32_t flags);

/**
 * @brief Request a system power-off.
 * @return KABI_EPERM if lacking CAP_SHUTDOWN.
 */
int kabi_request_shutdown();

/**
 * @brief Request a system reset.
 * @return KABI_EPERM if lacking CAP_REBOOT.
 */
int kabi_request_reboot(reboot_reason_t reason);

/* --- Task Management --- */

/**
 * @brief Voluntarily terminate the current task.
 * @guarantee This function does not return.
 */
void kabi_task_exit() __attribute__((noreturn));

/**
 * @brief Yield the current task's CPU time.
 */
void kabi_yield();

/* Signal numbers — POSIX-compatible values */
#define KABI_SIGINT  2
#define KABI_SIGKILL 9
#define KABI_SIGTERM 15
#define KABI_SIGCHLD 17
typedef int kabi_signal_t;

/* Backward-compat aliases for existing kernel_shell.c callers */
#define KABI_SIGNAL_TERM KABI_SIGTERM
#define KABI_SIGNAL_KILL KABI_SIGKILL

/**
 * @brief Send a signal to a process.
 * @param pid Target process ID.
 * @param signal The signal to send.
 * @return KABI_SUCCESS or negative error code.
 * @guarantee TERM asks task to exit (policy-dependent); KILL causes immediate destruction.
 */
int kabi_task_signal(int pid, kabi_signal_t signal);

/**
 * @brief Register a SIGTERM/SIGINT handler EIP for the current task.
 * @param sig    KABI_SIGTERM or KABI_SIGINT only.
 * @param handler_eip  User-space function pointer (Ring-3 EIP), or 0 to reset.
 * @return KABI_SUCCESS or KABI_EINVAL if sig is SIGKILL/SIGCHLD.
 */
int kabi_sigaction(int sig, virt_addr_t handler_eip);

/**
 * @brief Spawn a new process from an entry point.
 * @return Positive PID on success, negative error code on failure.
 */
int kabi_spawn_process(virt_addr_t entry_point, virt_addr_t user_stack);
int kabi_sys_exec(const char *path);
int kabi_sys_execve(const char *path, char **argv);
int kabi_fork();
/**
 * @brief Print process table to console.
 */
void kabi_ps();

/**
 * @brief Block until all child processes of the current task have exited.
 */
void kabi_wait_for_children();

/* --- Process Enumeration (K-ABI v2) --- */

/**
 * @brief Snapshot of a task's information for monitoring.
 */
typedef struct {
    uint32_t id;
    virt_addr_t user_esp;
    virt_addr_t user_eip;
    virt_addr_t kernel_stack;
    uint32_t capabilities;
    uint32_t parent_id;
    uint32_t ticks;
    uint8_t state;
    uint8_t _padding[3];
} kabi_task_info_t;

/* Task States */
#define KABI_TASK_READY   0
#define KABI_TASK_RUNNING 1
#define KABI_TASK_WAITING 2
#define KABI_TASK_ZOMBIE  3

/**
 * @brief Opaque iterator for traversing tasks.
 */
typedef struct {
    void *current;
    void *start;
    int first;
} kabi_task_iter_t;

/**
 * @brief Initialize a task iterator.
 * @return KABI_SUCCESS if tasks exist, or negative error code.
 */
int kabi_task_iter_begin(kabi_task_iter_t *it);

/**
 * @brief Get the next task's info and advance the iterator.
 * @param it Iterator state.
 * @param info Output struct for task details.
 * @return 1 if valid info returned, 0 if iteration complete.
 */
int kabi_task_next(kabi_task_iter_t *it, kabi_task_info_t *info);

/**
 * @brief Transition the current task to Ring 3.
 * @guarantee Target memory must be pre-mapped for user access.
 */
void kabi_jump_to_user_mode(virt_addr_t entry, virt_addr_t stack) __attribute__((noreturn));

/**
 * @brief Signal all non-privileged child processes for termination.
 */
void kabi_kill_all_children();

/* --- Scheduler Management --- */

typedef struct {
    const char *name;
    uint32_t version;

    /**
     * @brief Pick the next task to run.
     * @param out [OUT] Pointer to receive the next task handle.
     * @return KABI_SUCCESS or error.
     */
    int (*pick_next)(kabi_task_t **out);

    void (*on_task_added)(kabi_task_t *task);
    void (*on_task_removed)(kabi_task_t *task);
} kabi_scheduler_ops_t;

/**
 * @brief Register a new scheduler policy.
 */
void kabi_scheduler_register(kabi_scheduler_ops_t *ops);

/* --- VFS Management --- */

typedef struct kabi_dirent {
    char name[128];
    uint32_t ino;
    uint32_t size;
    uint8_t type;
    uint8_t attr;
} kabi_dirent_t;

// Seek modes
#define KABI_SEEK_SET  0
#define KABI_SEEK_CUR  1
#define KABI_SEEK_END  2

typedef struct kabi_fs_ops {
    uint32_t (*read)(kabi_fs_node_t* node, uint32_t offset, uint32_t size, uint8_t *buffer);
    uint32_t (*write)(kabi_fs_node_t* node, uint32_t offset, uint32_t size, uint8_t *buffer);
    void (*open)(kabi_fs_node_t* node);
    void (*close)(kabi_fs_node_t* node);
    kabi_dirent_t* (*readdir)(kabi_fs_node_t* node, uint32_t index);
    kabi_fs_node_t* (*finddir)(kabi_fs_node_t* node, char *name);
    void (*create)(kabi_fs_node_t* node, char *name, uint16_t mask);
    void (*mkdir)(kabi_fs_node_t* node, char *name, uint16_t mask);
    int  (*unlink)(kabi_fs_node_t* node, char *name);
} kabi_fs_ops_t;

void kabi_vfs_register(kabi_fs_ops_t *ops, const char *mountpoint);

/* --- VFS User API --- */
kabi_fs_node_t* kabi_vfs_get_root();
kabi_fs_node_t* kabi_vfs_resolve_path(const char *path);
uint32_t kabi_vfs_read(kabi_fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t kabi_vfs_write(kabi_fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
kabi_dirent_t* kabi_vfs_readdir(kabi_fs_node_t *node, uint32_t index);
kabi_fs_node_t* kabi_vfs_finddir(kabi_fs_node_t *node, char *name);

/* --- Interrupt & Hardware --- */

typedef void (*kabi_irq_handler_t)(void *regs);
void kabi_irq_register(uint8_t n, kabi_irq_handler_t handler);

/**
 * @brief Output a null-terminated string to the system console.
 */
void kprint(char *c);
void kabi_get_line(char *buf);
void kabi_int_to_ascii(int n, char str[]);
void kabi_hex_to_ascii(uint32_t n, char str[]);
void kabi_clear_screen();
int kabi_block_read(uint32_t dev, uint32_t lba, uint8_t *buf);
int kabi_block_write(uint32_t dev, uint32_t lba, uint8_t *buf);

/**
 * @brief Get the number of available block devices.
 */
int kabi_get_device_count();

/**
 * @brief Get the name of a device by index.
 * @return 0 on success, KABI_EINVAL if index out of range.
 */
int kabi_get_device_name(int index, char *buf);

/**
 * @brief Get the size of a device in sectors.
 * @return Size in 512-byte sectors, or 0 if unknown/invalid.
 */
uint64_t kabi_get_device_size(int index);

/* --- Time --- */
uint32_t kabi_get_ticks();

/* --- Debug & Testing --- */

#ifdef KABI_DEBUG
/**
 * @brief Command IDs for debugging.
 */
#define KABI_DEBUG_TEST_PAGING  1
#define KABI_DEBUG_TEST_STRESS  2
#define KABI_DEBUG_TEST_CORE    3

/**
 * @brief Invoke a internal kernel test.
 * @param test_id The ID of the test category.
 * @param args Implementation-defined arguments string.
 */
void kabi_debug_run_test(uint32_t test_id, const char *args);
#endif

int kabi_debug_enabled();
void kabi_set_debug(int enabled);

#endif // KABI_V1_H
