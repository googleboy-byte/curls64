#include "syscall_dispatch.h"
#include "pipe.h"
#include "task.h"
#include "../../libc/string.h"
#include "vfs_core.h"
#include "../ktrace/ktrace.h"
#include "uabi_helpers.h"
#include "../modules/drivers/keyboard.h"
#include "signal.h"
#include "abi_validate.h"
#include "block_dev.h"

static void syscall_handler(registers_t *regs) {
    assert_on_kstack(regs);
    
    uint32_t syscall_num = REGS_SYSNO(regs);
    KTRACE1(KTRACE_SYSCALL_ENTER, syscall_num);
    
    /* ── Legacy Syscalls (0–11) ────────────────────────────────── */

    if (syscall_num <= 11) {
        kprint("PANIC: Stray call to legacy syscall range (0-11): ");
        char s[10]; int_to_ascii(syscall_num, s); kprint(s); kprint("\n");
        REGS_SYSNO(regs) = (uintptr_t)(-1);
        while(1) { asm volatile("sti; hlt"); }
    }

    /* ── U-ABI v1 Syscalls (20–53) ────────────────────────────── */

    else if (syscall_num == 20) { /* UABI_OPEN */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = open((char *)REGS_ARG1(regs), REGS_ARG2(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 21) { /* UABI_READ */
        UABI_VALIDATE_FD(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_PTR(REGS_ARG2(regs), syscall_num);
        UABI_VALIDATE_POSITIVE(REGS_ARG3(regs), syscall_num);
        REGS_SYSNO(regs) = read(REGS_ARG1(regs), (char *)REGS_ARG2(regs), REGS_ARG3(regs));
        if (kabi_debug_enabled() && REGS_ARG1(regs) == 0 && REGS_SYSNO(regs) > 0) {
            char s[16]; hex_to_ascii(current_task->id, s);
            kprint("[READ] PID 0x"); kprint(s);
            kprint(" FD0 size="); int_to_ascii(REGS_SYSNO(regs), s); kprint(s);
            kprint(" data="); kprint((char *)REGS_ARG2(regs)); kprint("\n");
        }
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 22) { /* UABI_WRITE */
        UABI_VALIDATE_FD(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_PTR(REGS_ARG2(regs), syscall_num);
        UABI_VALIDATE_POSITIVE(REGS_ARG3(regs), syscall_num);
        if (kabi_debug_enabled() && REGS_ARG1(regs) == 1) {
            char s[16]; hex_to_ascii(current_task->id, s);
            kprint("[WRITE] PID 0x"); kprint(s);
            kprint(" FD1 size="); int_to_ascii(REGS_ARG3(regs), s); kprint(s); kprint("\n");
        }
        REGS_SYSNO(regs) = write(REGS_ARG1(regs), (char *)REGS_ARG2(regs), REGS_ARG3(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 23) { /* UABI_CLOSE */
        UABI_VALIDATE_FD(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = close(REGS_ARG1(regs));
    } else if (syscall_num == 24) { /* UABI_READDIR */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_PTR(REGS_ARG2(regs), syscall_num);
        UABI_VALIDATE_POSITIVE(REGS_ARG3(regs), syscall_num);
        REGS_SYSNO(regs) = sys_readdir((const char *)REGS_ARG1(regs), (void *)REGS_ARG2(regs), REGS_ARG3(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 25) { /* UABI_GETCWD */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_POSITIVE(REGS_ARG2(regs), syscall_num);
        REGS_SYSNO(regs) = sys_getcwd((char *)REGS_ARG1(regs), REGS_ARG2(regs));
    } else if (syscall_num == 26) { /* UABI_CHDIR */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = sys_chdir((const char *)REGS_ARG1(regs));
    } else if (syscall_num == 27) { /* UABI_STAT */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_PTR(REGS_ARG2(regs), syscall_num);
        REGS_SYSNO(regs) = sys_stat((const char *)REGS_ARG1(regs), (void *)REGS_ARG2(regs));
        if (kabi_debug_enabled() && REGS_SYSNO(regs) != 0) {
             kprint("[STAT] fail path="); kprint((const char *)REGS_ARG1(regs)); kprint("\n");
        }
    } else if (syscall_num == 28) { /* UABI_LSEEK */
        UABI_VALIDATE_FD(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_RANGE(REGS_ARG3(regs), 0, 2, syscall_num);
        REGS_SYSNO(regs) = seek(REGS_ARG1(regs), REGS_ARG2(regs), REGS_ARG3(regs));
    } else if (syscall_num == 30) { /* UABI_FORK */
        REGS_SYSNO(regs) = sys_fork(regs);
    } else if (syscall_num == 31) { /* UABI_EXEC */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        if (kabi_debug_enabled()) {
            char s[16]; hex_to_ascii(current_task->id, s);
            kprint("[EXEC] PID 0x"); kprint(s);
            kprint(" path="); kprint((const char *)REGS_ARG1(regs)); kprint("\n");
        }
        REGS_SYSNO(regs) = sys_execve((const char *)REGS_ARG1(regs), (char **)REGS_ARG2(regs), regs);
    } else if (syscall_num == 32) { /* UABI_EXIT */
        current_task->exit_code = (int)REGS_ARG1(regs);
        if (kabi_debug_enabled()) {
            char s[16]; hex_to_ascii(current_task->id, s);
            kprint("[EXIT] PID 0x"); kprint(s);
            kprint(" code="); int_to_ascii(current_task->exit_code, s); kprint(s); kprint("\n");
        }
        kill(getpid());
        schedule(regs);
        while(1) { asm volatile("sti; hlt"); }
    } else if (syscall_num == 33) { /* UABI_WAIT */
        REGS_SYSNO(regs) = wait_for_children();
    } else if (syscall_num == 34) { /* UABI_GETPID */
        REGS_SYSNO(regs) = getpid();
    } else if (syscall_num == 35) { /* UABI_PIPE */
        extern int pipe(int fds[2]);
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = pipe((int *)REGS_ARG1(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 36) { /* UABI_DUP2 */
        extern int dup2(int oldfd, int newfd);
        UABI_VALIDATE_FD(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_FD(REGS_ARG2(regs), syscall_num);
        REGS_SYSNO(regs) = dup2(REGS_ARG1(regs), REGS_ARG2(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 37) { /* UABI_DUP */
        extern int dup(int oldfd);
        UABI_VALIDATE_FD(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = dup(REGS_ARG1(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 40) { /* UABI_PRINT */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        kprint((char *)REGS_ARG1(regs));
    } else if (syscall_num == 41) { /* UABI_GETLINE */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        get_line((char *)REGS_ARG1(regs));
    } else if (syscall_num == 42) { /* UABI_CLEAR */
        extern void clear_screen();
        clear_screen();
    } else if (syscall_num == 43) { /* UABI_GETC */
        extern int sys_getchar(void);
        REGS_SYSNO(regs) = sys_getchar();
        if (kabi_debug_enabled()) {
            char s[16];
            kprint("[GETC] char='"); 
            char buf[2] = {(char)REGS_SYSNO(regs), 0};
            if (buf[0] >= 32 && buf[0] <= 126) kprint(buf);
            else { kprint("0x"); hex_to_ascii((uint32_t)REGS_SYSNO(regs), s); kprint(s); }
            kprint("'\n");
        }
    } else if (syscall_num == 44) { /* UABI_GOTOXY */
        extern void set_cursor_position(int col, int row);
        UABI_VALIDATE_RANGE(REGS_ARG1(regs), 0, 79, syscall_num);
        UABI_VALIDATE_RANGE(REGS_ARG2(regs), 0, 24, syscall_num);
        set_cursor_position(REGS_ARG1(regs), REGS_ARG2(regs));
        REGS_SYSNO(regs) = 0;
    } else if (syscall_num == 45) { /* UABI_PS */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_POSITIVE(REGS_ARG2(regs), syscall_num);
        REGS_SYSNO(regs) = sys_ps((void *)REGS_ARG1(regs), REGS_ARG2(regs));
        UABI_VALIDATE_OUTPUT(REGS_SYSNO(regs), syscall_num);
    } else if (syscall_num == 46) { /* UABI_MEMSTAT */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = sys_memstat((void *)REGS_ARG1(regs));
    } else if (syscall_num == 47) { /* UABI_SET_DEBUG */
        extern int kabi_debug_enabled();
        extern void kabi_set_debug(int enabled);
        if (REGS_ARG2(regs)) { /* Query */
            REGS_SYSNO(regs) = kabi_debug_enabled();
        } else { /* Set */
            kabi_set_debug(REGS_ARG1(regs));
            REGS_SYSNO(regs) = 0;
        }
    } else if (syscall_num == 48) { /* UABI_KILL */
        UABI_VALIDATE_POSITIVE(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_RANGE(REGS_ARG2(regs), 1, 31, syscall_num);
        REGS_SYSNO(regs) = task_send_signal((int)REGS_ARG1(regs), (int)REGS_ARG2(regs));
    } else if (syscall_num == 49) { /* UABI_SIGACTION */
        int sig = (int)REGS_ARG1(regs);
        uint32_t handler = REGS_ARG2(regs);
        /* Only SIGTERM(15) and SIGINT(2) can have user handlers */
        if (sig == SIGKILL || sig == SIGCHLD) {
            kprint("[VALIDATE FAIL] syscall 49: cannot set handler for SIGKILL/SIGCHLD\n");
            REGS_SYSNO(regs) = -1;
        } else {
            current_task->sigterm_handler = handler;
            REGS_SYSNO(regs) = 0;
        }
    } else if (syscall_num == 50) { /* UABI_SIGRETURN */
        /* Restore user context saved before signal handler dispatch */
        if (current_task->in_signal) {
            REGS_IP(regs) = current_task->saved_eip;
            REGS_SP(regs) = current_task->saved_esp;
            current_task->in_signal = 0;
        }
        REGS_SYSNO(regs) = 0;
    } else if (syscall_num == 51) { /* UABI_SLEEP */
        uint32_t ms = REGS_ARG1(regs);
        if (ms == 0) {
            /* sleep(0): just yield to next task */
            REGS_SYSNO(regs) = 0;
        } else {
            /* Timer runs at 100 Hz → 1 tick = 10 ms.
             * Round up so sleep(1) still waits at least 1 tick. */
            extern uint32_t get_ticks();
            extern void sleepq_insert(task_t *t, uint32_t wake_tick);
            uint32_t ticks = (ms + 9) / 10;
            sleepq_insert((task_t*)current_task, get_ticks() + ticks);
            current_task->state = TASK_WAITING;
            REGS_SYSNO(regs) = 0;
            /* Release to scheduler so another task runs while we sleep */
            task_check_pending_signals(regs); /* honour any pending signal first */
            schedule(regs);
        }
    } else if (syscall_num == 52) { /* UABI_MKDIR */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = sys_mkdir((const char *)REGS_ARG1(regs));
    } else if (syscall_num == 53) { /* UABI_UNLINK */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = sys_unlink((const char *)REGS_ARG1(regs));
    } else if (syscall_num == 54) { /* UABI_DEVINFO */
        extern int block_dev_get_count(void);
        extern kabi_block_device_t* block_dev_get_by_index(int index);
        int index = (int)REGS_ARG1(regs);
        if (index == -1) {
            /* Return device count */
            REGS_SYSNO(regs) = block_dev_get_count();
        } else {
            UABI_VALIDATE_PTR(REGS_ARG2(regs), syscall_num);
            kabi_block_device_t *dev = block_dev_get_by_index(index);
            if (!dev) {
                REGS_SYSNO(regs) = (uintptr_t)(-2); /* ENOENT */
            } else {
                /* Copy info to userspace struct */
                typedef struct { char name[32]; uint32_t sectors; uint32_t sector_size; int is_partition; int parent_dev; } uinfo_t;
                uinfo_t *uinfo = (uinfo_t *)REGS_ARG2(regs);
                /* Copy name */
                for (int i = 0; i < 31 && dev->name[i]; i++) {
                    uinfo->name[i] = dev->name[i];
                    uinfo->name[i+1] = '\0';
                }
                uinfo->sectors = dev->size;
                uinfo->sector_size = 512;
                uinfo->is_partition = dev->is_partition;
                uinfo->parent_dev = (int)dev->parent_dev;
                REGS_SYSNO(regs) = 0;
            }
        }
    } else if (syscall_num == 55) { /* UABI_MOUNT */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        UABI_VALIDATE_PTR(REGS_ARG2(regs), syscall_num);
        const char *dev_name = (const char *)REGS_ARG1(regs);
        const char *mountpoint = (const char *)REGS_ARG2(regs);

        /* Resolve device by name */
        extern int block_dev_get_count(void);
        extern kabi_block_device_t* block_dev_get_by_index(int index);
        int count = block_dev_get_count();
        int dev_id = -1;
        for (int i = 0; i < count; i++) {
            kabi_block_device_t *d = block_dev_get_by_index(i);
            if (d) {
                /* Simple strcmp */
                const char *a = dev_name;
                const char *b = d->name;
                int match = 1;
                while (*a && *b) {
                    if (*a++ != *b++) { match = 0; break; }
                }
                if (match && *a == '\0' && *b == '\0') {
                    dev_id = i;
                    break;
                }
            }
        }

        if (dev_id < 0) {
            REGS_SYSNO(regs) = (uintptr_t)(-2); /* ENOENT */
        } else {
            extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
            REGS_SYSNO(regs) = fat32_vfs_mount(dev_id, mountpoint) == 0 ? 0 : (uintptr_t)(-4);
        }
    } else if (syscall_num == 56) { /* UABI_UMOUNT */
        UABI_VALIDATE_PTR(REGS_ARG1(regs), syscall_num);
        REGS_SYSNO(regs) = sys_umount((const char *)REGS_ARG1(regs));
    } else {
        kprint("[VALIDATE FAIL] Unknown syscall: ");
        char ss[10];
        int_to_ascii(syscall_num, ss);
        kprint(ss);
        kprint("\n");
        REGS_SYSNO(regs) = (uintptr_t)(-1);
    }

syscall_done:
    /* Check for pending signals and inject trampoline if returning to Ring-3 */
    task_check_pending_signals(regs);

    KTRACE2(KTRACE_SYSCALL_EXIT, syscall_num, REGS_SYSNO(regs));
}

void init_syscalls() {
    register_interrupt_handler(0x80, syscall_handler);
}
