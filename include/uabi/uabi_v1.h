#ifndef UABI_V1_H
#define UABI_V1_H

#include <stdint.h>

// ============================================================================
// U-ABI v1: User-Space ABI for Ring 3 Programs
// ============================================================================
// This interface provides syscall-based access to kernel services for
// user-space programs running in Ring 3. All functions use int 0x80.

// VFS Operations (20-29)
#define UABI_OPEN      20
#define UABI_READ      21
#define UABI_WRITE     22
#define UABI_CLOSE     23
#define UABI_READDIR   24
#define UABI_GETCWD    25
#define UABI_CHDIR     26
#define UABI_STAT      27
#define UABI_LSEEK     28
#define UABI_MKDIR     52
#define UABI_UNLINK    53

// Process Operations (30-39)
#define UABI_FORK      30
#define UABI_EXEC      31
#define UABI_EXIT      32
#define UABI_WAIT      33
#define UABI_PIPE      35
#define UABI_DUP2      36
#define UABI_DUP       37
#define UABI_GETPID    34

// I/O Operations (40-44)
#define UABI_PRINT     40
#define UABI_GETLINE   41
#define UABI_CLEAR     42
#define UABI_GETC      43
#define UABI_GOTOXY    44

// System Info (45-49)
#define UABI_PS        45
#define UABI_MEMSTAT   46
#define UABI_SET_DEBUG 47
#define UABI_KILL      48   /* kill(pid, sig) */
#define UABI_SIGACTION 49   /* sigaction(sig, handler) */
#define UABI_SIGRETURN 50   /* sigreturn() — called by trampoline, not user code */
#define UABI_SLEEP     51   /* sleep(ms) — sleep for N milliseconds */

// Block Device / Mount Operations (54-55)
#define UABI_DEVINFO   54   /* devinfo(index, info) — enumerate block devices */
#define UABI_MOUNT     55   /* mount(device, mountpoint) — mount a filesystem */
#define UABI_UMOUNT    56   /* umount(mountpoint) — unmount a filesystem */

// Special Key Codes
#define UABI_KEY_UP    0x81
#define UABI_KEY_DOWN  0x82
#define UABI_KEY_LEFT  0x83
#define UABI_KEY_RIGHT 0x84
#define UABI_KEY_CTRL_UP    0x85
#define UABI_KEY_CTRL_DOWN  0x86
#define UABI_KEY_CTRL_LEFT  0x87
#define UABI_KEY_CTRL_RIGHT 0x88

// Return codes
#define UABI_SUCCESS   0
#define UABI_ERROR    -1
#define UABI_ENOENT   -2
#define UABI_EINVAL   -3
#define UABI_EIO      -4
#define UABI_ENOMEM   -5

// File flags
#define UABI_O_RDONLY  0x0000
#define UABI_O_WRONLY  0x0001
#define UABI_O_RDWR    0x0002
#define UABI_O_CREAT   0x0040
#define UABI_O_TRUNC   0x0200

// Seek modes
#define UABI_SEEK_SET  0
#define UABI_SEEK_CUR  1
#define UABI_SEEK_END  2

// Directory entry structure
typedef struct {
    char name[128];
    uint32_t inode;
    uint32_t size;
    uint8_t type;
    uint8_t attr;
} uabi_dirent_t;

// File stat structure
typedef struct {
    uint32_t size;
    uint32_t inode;
    uint8_t type;
} uabi_stat_t;

// Block device info structure
typedef struct {
    char name[32];
    uint32_t sectors;
    uint32_t sector_size;
    int is_partition;
    int parent_dev;
} uabi_devinfo_t;

// Process info structure
typedef struct {
    int pid;
    int parent_pid;
    int state;
    uint32_t user_eip;
    uint32_t user_esp;
    uint32_t ticks;
} uabi_proc_info_t;

// Memory stats structure
typedef struct {
    uint32_t total_frames;
    uint32_t used_frames;
    uint32_t free_frames;
} uabi_memstat_t;

// ============================================================================
// U-ABI v1 Function Prototypes (implemented in uabi_syscalls.s)
// ============================================================================

// VFS Operations
int uabi_open(const char *path, int flags);
int uabi_read(int fd, void *buf, uint32_t count);
int uabi_write(int fd, const void *buf, uint32_t count);
int uabi_close(int fd);
int uabi_readdir(const char *path, uabi_dirent_t *entries, int max_entries);
int uabi_getcwd(char *buf, uint32_t size);
int uabi_chdir(const char *path);
int uabi_stat(const char *path, uabi_stat_t *stat);
int uabi_lseek(int fd, int offset, int whence);
int uabi_mkdir(const char *path);
int uabi_unlink(const char *path);

// Process Operations
int uabi_fork(void);
int uabi_exec(const char *path, char **argv);
void uabi_exit(int code) __attribute__((noreturn));
int uabi_wait(void);
int uabi_getpid(void);
int uabi_pipe(int fds[2]);
int uabi_dup2(int oldfd, int newfd);
int uabi_dup(int oldfd);

// I/O Operations
void uabi_print(const char *str);
void uabi_getline(char *buf, uint32_t max_len);
void uabi_clear(void);
int uabi_getc(void);
void uabi_gotoxy(int col, int row);

// System Info
int uabi_ps(uabi_proc_info_t *procs, int max_procs);
int uabi_memstat(uabi_memstat_t *stat);
int uabi_debug_enabled();
void uabi_set_debug(int enabled);
int uabi_kill(int pid, int sig);
int uabi_sigaction(int sig, void (*handler)(int));
void uabi_sleep(uint32_t ms);

// Block Device / Mount
int uabi_devinfo(int index, uabi_devinfo_t *info);
int uabi_mount(const char *device, const char *mountpoint);
int uabi_umount(const char *mountpoint);

#endif
