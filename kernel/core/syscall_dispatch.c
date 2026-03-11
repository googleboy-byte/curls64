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
    
    uint32_t syscall_num = regs->eax;
    KTRACE1(KTRACE_SYSCALL_ENTER, syscall_num);
    
    /* ── Legacy Syscalls (0–11) ────────────────────────────────── */

    if (syscall_num == SYS_EXIT) {
        kill(getpid());
        schedule(regs);
        // Should never reach here
        kprint("EXIT FAILED! PID: ");
        char s[10]; int_to_ascii(getpid(), s); kprint(s); kprint("\n");
        while(1) { asm volatile("sti; hlt"); }
    } else if (syscall_num == SYS_PRINT) {
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        kprint((char *)regs->ebx);
    } else if (syscall_num == SYS_OPEN) {
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = open((char *)regs->ebx, regs->ecx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == SYS_CLOSE) {
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        regs->eax = close(regs->ebx);
    } else if (syscall_num == SYS_READ) {
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->edx, syscall_num);
        regs->eax = read(regs->ebx, (char *)regs->ecx, regs->edx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == SYS_WRITE) {
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->edx, syscall_num);
        regs->eax = write(regs->ebx, (char *)regs->ecx, regs->edx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == SYS_SEEK) {
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_RANGE(regs->edx, 0, 2, syscall_num);
        regs->eax = seek(regs->ebx, regs->ecx, regs->edx);
    } else if (syscall_num == SYS_DUP) {
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        regs->eax = dup(regs->ebx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == SYS_DUP2) {
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_FD(regs->ecx, syscall_num);
        regs->eax = dup2(regs->ebx, regs->ecx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == SYS_PIPE) {
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = pipe((int*)regs->ebx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == SYS_FORK) {
        regs->eax = sys_fork(regs);
    } else if (syscall_num == SYS_EXECVE) {
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_execve((const char *)regs->ebx, (char **)regs->ecx, regs);
    }

    /* ── U-ABI v1 Syscalls (20–53) ────────────────────────────── */

    else if (syscall_num == 20) { /* UABI_OPEN */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = open((char *)regs->ebx, regs->ecx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 21) { /* UABI_READ */
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->edx, syscall_num);
        regs->eax = read(regs->ebx, (char *)regs->ecx, regs->edx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 22) { /* UABI_WRITE */
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->edx, syscall_num);
        regs->eax = write(regs->ebx, (char *)regs->ecx, regs->edx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 23) { /* UABI_CLOSE */
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        regs->eax = close(regs->ebx);
    } else if (syscall_num == 24) { /* UABI_READDIR */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->edx, syscall_num);
        regs->eax = sys_readdir((const char *)regs->ebx, (void *)regs->ecx, regs->edx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 25) { /* UABI_GETCWD */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->ecx, syscall_num);
        regs->eax = sys_getcwd((char *)regs->ebx, regs->ecx);
    } else if (syscall_num == 26) { /* UABI_CHDIR */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_chdir((const char *)regs->ebx);
    } else if (syscall_num == 27) { /* UABI_STAT */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        regs->eax = sys_stat((const char *)regs->ebx, (void *)regs->ecx);
    } else if (syscall_num == 28) { /* UABI_LSEEK */
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_RANGE(regs->edx, 0, 2, syscall_num);
        regs->eax = seek(regs->ebx, regs->ecx, regs->edx);
    } else if (syscall_num == 30) { /* UABI_FORK */
        regs->eax = sys_fork(regs);
    } else if (syscall_num == 31) { /* UABI_EXEC */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_execve((const char *)regs->ebx, (char **)regs->ecx, regs);
    } else if (syscall_num == 32) { /* UABI_EXIT */
        current_task->exit_code = (int)regs->ebx;
        if (kabi_debug_enabled()) {
            char s[16]; hex_to_ascii(current_task->id, s);
            kprint("[EXIT] PID 0x"); kprint(s);
            kprint(" code="); int_to_ascii(current_task->exit_code, s); kprint(s); kprint("\n");
        }
        kill(getpid());
        schedule(regs);
        while(1) { asm volatile("sti; hlt"); }
    } else if (syscall_num == 33) { /* UABI_WAIT */
        regs->eax = wait_for_children();
    } else if (syscall_num == 34) { /* UABI_GETPID */
        regs->eax = getpid();
    } else if (syscall_num == 35) { /* UABI_PIPE */
        extern int pipe(int fds[2]);
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = pipe((int *)regs->ebx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 36) { /* UABI_DUP2 */
        extern int dup2(int oldfd, int newfd);
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        UABI_VALIDATE_FD(regs->ecx, syscall_num);
        regs->eax = dup2(regs->ebx, regs->ecx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 37) { /* UABI_DUP */
        extern int dup(int oldfd);
        UABI_VALIDATE_FD(regs->ebx, syscall_num);
        regs->eax = dup(regs->ebx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 40) { /* UABI_PRINT */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        kprint((char *)regs->ebx);
    } else if (syscall_num == 41) { /* UABI_GETLINE */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        get_line((char *)regs->ebx);
    } else if (syscall_num == 42) { /* UABI_CLEAR */
        extern void clear_screen();
        clear_screen();
    } else if (syscall_num == 43) { /* UABI_GETC */
        extern int sys_getchar(void);
        regs->eax = sys_getchar();
    } else if (syscall_num == 44) { /* UABI_GOTOXY */
        extern void set_cursor_position(int col, int row);
        UABI_VALIDATE_RANGE(regs->ebx, 0, 79, syscall_num);
        UABI_VALIDATE_RANGE(regs->ecx, 0, 24, syscall_num);
        set_cursor_position(regs->ebx, regs->ecx);
        regs->eax = 0;
    } else if (syscall_num == 45) { /* UABI_PS */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        UABI_VALIDATE_POSITIVE(regs->ecx, syscall_num);
        regs->eax = sys_ps((void *)regs->ebx, regs->ecx);
        UABI_VALIDATE_OUTPUT(regs->eax, syscall_num);
    } else if (syscall_num == 46) { /* UABI_MEMSTAT */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_memstat((void *)regs->ebx);
    } else if (syscall_num == 47) { /* UABI_SET_DEBUG */
        extern int kabi_debug_enabled();
        extern void kabi_set_debug(int enabled);
        if (regs->ecx) { /* Query */
            regs->eax = kabi_debug_enabled();
        } else { /* Set */
            kabi_set_debug(regs->ebx);
            regs->eax = 0;
        }
    } else if (syscall_num == 48) { /* UABI_KILL */
        UABI_VALIDATE_POSITIVE(regs->ebx, syscall_num);
        UABI_VALIDATE_RANGE(regs->ecx, 1, 31, syscall_num);
        regs->eax = task_send_signal((int)regs->ebx, (int)regs->ecx);
    } else if (syscall_num == 49) { /* UABI_SIGACTION */
        int sig = (int)regs->ebx;
        uint32_t handler = regs->ecx;
        /* Only SIGTERM(15) and SIGINT(2) can have user handlers */
        if (sig == SIGKILL || sig == SIGCHLD) {
            kprint("[VALIDATE FAIL] syscall 49: cannot set handler for SIGKILL/SIGCHLD\n");
            regs->eax = -1;
        } else {
            current_task->sigterm_handler = handler;
            regs->eax = 0;
        }
    } else if (syscall_num == 50) { /* UABI_SIGRETURN */
        /* Restore user context saved before signal handler dispatch */
        if (current_task->in_signal) {
            regs->eip = current_task->saved_eip;
            regs->esp = current_task->saved_esp;
            current_task->in_signal = 0;
        }
        regs->eax = 0;
    } else if (syscall_num == 51) { /* UABI_SLEEP */
        uint32_t ms = regs->ebx;
        if (ms == 0) {
            /* sleep(0): just yield to next task */
            regs->eax = 0;
        } else {
            /* Timer runs at 100 Hz → 1 tick = 10 ms.
             * Round up so sleep(1) still waits at least 1 tick. */
            extern uint32_t get_ticks();
            extern void sleepq_insert(task_t *t, uint32_t wake_tick);
            uint32_t ticks = (ms + 9) / 10;
            sleepq_insert((task_t*)current_task, get_ticks() + ticks);
            current_task->state = TASK_WAITING;
            regs->eax = 0;
            /* Release to scheduler so another task runs while we sleep */
            task_check_pending_signals(regs); /* honour any pending signal first */
            schedule(regs);
        }
    } else if (syscall_num == 52) { /* UABI_MKDIR */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_mkdir((const char *)regs->ebx);
    } else if (syscall_num == 53) { /* UABI_UNLINK */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_unlink((const char *)regs->ebx);
    } else if (syscall_num == 54) { /* UABI_DEVINFO */
        extern int block_dev_get_count(void);
        extern kabi_block_device_t* block_dev_get_by_index(int index);
        int index = (int)regs->ebx;
        if (index == -1) {
            /* Return device count */
            regs->eax = block_dev_get_count();
        } else {
            UABI_VALIDATE_PTR(regs->ecx, syscall_num);
            kabi_block_device_t *dev = block_dev_get_by_index(index);
            if (!dev) {
                regs->eax = (uint32_t)(-2); /* ENOENT */
            } else {
                /* Copy info to userspace struct */
                typedef struct { char name[32]; uint32_t sectors; uint32_t sector_size; int is_partition; int parent_dev; } uinfo_t;
                uinfo_t *uinfo = (uinfo_t *)regs->ecx;
                /* Copy name */
                for (int i = 0; i < 31 && dev->name[i]; i++) {
                    uinfo->name[i] = dev->name[i];
                    uinfo->name[i+1] = '\0';
                }
                uinfo->sectors = dev->size;
                uinfo->sector_size = 512;
                uinfo->is_partition = dev->is_partition;
                uinfo->parent_dev = (int)dev->parent_dev;
                regs->eax = 0;
            }
        }
    } else if (syscall_num == 55) { /* UABI_MOUNT */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        UABI_VALIDATE_PTR(regs->ecx, syscall_num);
        const char *dev_name = (const char *)regs->ebx;
        const char *mountpoint = (const char *)regs->ecx;

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
            regs->eax = (uint32_t)(-2); /* ENOENT */
        } else {
            extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
            regs->eax = fat32_vfs_mount(dev_id, mountpoint) == 0 ? 0 : (uint32_t)(-4);
        }
    } else if (syscall_num == 56) { /* UABI_UMOUNT */
        UABI_VALIDATE_PTR(regs->ebx, syscall_num);
        regs->eax = sys_umount((const char *)regs->ebx);
    } else {
        kprint("[VALIDATE FAIL] Unknown syscall: ");
        char ss[10];
        int_to_ascii(syscall_num, ss);
        kprint(ss);
        kprint("\n");
        regs->eax = (uint32_t)(-1);
    }

syscall_done:
    /* Check for pending signals and inject trampoline if returning to Ring-3 */
    task_check_pending_signals(regs);

    KTRACE2(KTRACE_SYSCALL_EXIT, syscall_num, regs->eax);
}

void init_syscalls() {
    register_interrupt_handler(0x80, syscall_handler);
}
