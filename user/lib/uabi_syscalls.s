[bits 32]

; ============================================================================
; U-ABI v1 Syscall Wrappers
; ============================================================================
; All syscalls use int 0x80 with:
;   EAX = syscall number
;   EBX, ECX, EDX = arguments
;   Return value in EAX

section .text

; VFS Operations (20-29)
global uabi_open
global uabi_read
global uabi_write
global uabi_close
global uabi_readdir
global uabi_getcwd
global uabi_chdir
global uabi_stat
global uabi_lseek
global uabi_mkdir
global uabi_unlink

; Process Operations (30-39)
global uabi_fork
global uabi_exec
global uabi_exit
global uabi_wait
global uabi_getpid

; I/O Operations (40-44)
global uabi_print
global uabi_getline
global uabi_clear
global uabi_getc
global uabi_gotoxy

; System Info (45-49)
global uabi_ps
global uabi_memstat

; ----------------------------------------------------------------------------
; VFS Operations
; ----------------------------------------------------------------------------

; int uabi_open(const char *path, int flags)
uabi_open:
    push ebx
    mov eax, 20             ; UABI_OPEN
    mov ebx, [esp + 8]      ; path
    mov ecx, [esp + 12]     ; flags
    int 0x80
    pop ebx
    ret

; int uabi_read(int fd, void *buf, uint32_t count)
uabi_read:
    push ebx
    mov eax, 21             ; UABI_READ
    mov ebx, [esp + 8]      ; fd
    mov ecx, [esp + 12]     ; buf
    mov edx, [esp + 16]     ; count
    int 0x80
    pop ebx
    ret

; int uabi_write(int fd, const void *buf, uint32_t count)
uabi_write:
    push ebx
    mov eax, 22             ; UABI_WRITE
    mov ebx, [esp + 8]      ; fd
    mov ecx, [esp + 12]     ; buf
    mov edx, [esp + 16]     ; count
    int 0x80
    pop ebx
    ret

; int uabi_close(int fd)
uabi_close:
    push ebx
    mov eax, 23             ; UABI_CLOSE
    mov ebx, [esp + 8]      ; fd
    int 0x80
    pop ebx
    ret

; int uabi_readdir(const char *path, uabi_dirent_t *entries, int max_entries)
uabi_readdir:
    push ebx
    mov eax, 24             ; UABI_READDIR
    mov ebx, [esp + 8]      ; path
    mov ecx, [esp + 12]     ; entries
    mov edx, [esp + 16]     ; max_entries
    int 0x80
    pop ebx
    ret

; int uabi_getcwd(char *buf, uint32_t size)
uabi_getcwd:
    push ebx
    mov eax, 25             ; UABI_GETCWD
    mov ebx, [esp + 8]      ; buf
    mov ecx, [esp + 12]     ; size
    int 0x80
    pop ebx
    ret

; int uabi_chdir(const char *path)
uabi_chdir:
    push ebx
    mov eax, 26             ; UABI_CHDIR
    mov ebx, [esp + 8]      ; path
    int 0x80
    pop ebx
    ret

; int uabi_stat(const char *path, uabi_stat_t *stat)
uabi_stat:
    push ebx
    mov eax, 27             ; UABI_STAT
    mov ebx, [esp + 8]      ; path
    mov ecx, [esp + 12]     ; stat
    int 0x80
    pop ebx
    ret

; int uabi_lseek(int fd, int offset, int whence)
uabi_lseek:
    push ebx
    mov eax, 28             ; UABI_LSEEK
    mov ebx, [esp + 8]      ; fd
    mov ecx, [esp + 12]     ; offset
    mov edx, [esp + 16]     ; whence
    int 0x80
    pop ebx
    ret
    
; int uabi_mkdir(const char *path)
uabi_mkdir:
    push ebx
    mov eax, 52             ; UABI_MKDIR
    mov ebx, [esp + 8]      ; path
    int 0x80
    pop ebx
    ret

; int uabi_unlink(const char *path)
uabi_unlink:
    push ebx
    mov eax, 53             ; UABI_UNLINK
    mov ebx, [esp + 8]      ; path
    int 0x80
    pop ebx
    ret

; ----------------------------------------------------------------------------
; Process Operations
; ----------------------------------------------------------------------------

; int uabi_fork(void)
uabi_fork:
    mov eax, 30             ; UABI_FORK
    int 0x80
    ret

; int uabi_exec(const char *path, char **argv)
uabi_exec:
    push ebx
    mov eax, 31             ; UABI_EXEC
    mov ebx, [esp + 8]      ; path
    mov ecx, [esp + 12]     ; argv
    int 0x80
    pop ebx
    ret

; void uabi_exit(int code)
uabi_exit:
    mov eax, 32             ; UABI_EXIT
    mov ebx, [esp + 4]      ; code
    int 0x80
    ; Should never return
    jmp $

; int uabi_wait(void)
uabi_wait:
    mov eax, 33             ; UABI_WAIT
    int 0x80
    ret

; int uabi_getpid(void)
uabi_getpid:
    mov eax, 34             ; UABI_GETPID
    int 0x80
    ret

; ----------------------------------------------------------------------------
; I/O Operations
; ----------------------------------------------------------------------------

; void uabi_print(const char *str)
uabi_print:
    push ebx
    mov eax, 40             ; UABI_PRINT
    mov ebx, [esp + 8]      ; str
    int 0x80
    pop ebx
    ret

; void uabi_getline(char *buf, uint32_t max_len)
uabi_getline:
    push ebx
    mov eax, 41             ; UABI_GETLINE
    mov ebx, [esp + 8]      ; buf
    mov ecx, [esp + 12]     ; max_len
    int 0x80
    pop ebx
    ret

; void uabi_clear(void)
uabi_clear:
    mov eax, 42             ; UABI_CLEAR
    int 0x80
    ret

; int uabi_getc(void)
uabi_getc:
    mov eax, 43             ; UABI_GETC
    int 0x80
    ret

; void uabi_gotoxy(int col, int row)
uabi_gotoxy:
    push ebx
    mov eax, 44             ; UABI_GOTOXY
    mov ebx, [esp + 8]      ; col
    mov ecx, [esp + 12]     ; row
    int 0x80
    pop ebx
    ret

; ----------------------------------------------------------------------------
; System Info
; ----------------------------------------------------------------------------

; int uabi_ps(uabi_proc_info_t *procs, int max_procs)
uabi_ps:
    push ebx
    mov eax, 45             ; UABI_PS
    mov ebx, [esp + 8]      ; procs
    mov ecx, [esp + 12]     ; max_procs
    int 0x80
    pop ebx
    ret

; int uabi_memstat(uabi_memstat_t *stat)
uabi_memstat:
    push ebx
    mov eax, 46             ; UABI_MEMSTAT
    mov ebx, [esp + 8]      ; stat
    int 0x80
    pop ebx
    ret
; int uabi_debug_enabled(void)
uabi_debug_enabled:
    push ebx
    mov eax, 47             ; UABI_SET_DEBUG
    mov ecx, 1              ; Query flag
    int 0x80
    pop ebx
    ret

; void uabi_set_debug(int enabled)
uabi_set_debug:
    push ebx
    mov eax, 47             ; UABI_SET_DEBUG
    mov ebx, [esp + 8]      ; enabled
    mov ecx, 0              ; Set flag
    int 0x80
    pop ebx
    ret

global uabi_debug_enabled
global uabi_set_debug
; int uabi_pipe(int fds[2])
uabi_pipe:
    push ebx
    mov eax, 35             ; UABI_PIPE
    mov ebx, [esp + 8]      ; fds
    int 0x80
    pop ebx
    ret

global uabi_pipe
; int uabi_dup2(int oldfd, int newfd)
uabi_dup2:
    push ebx
    mov eax, 36             ; UABI_DUP2
    mov ebx, [esp + 8]      ; oldfd
    mov ecx, [esp + 12]     ; newfd
    int 0x80
    pop ebx
    ret

global uabi_dup2
; int uabi_dup(int oldfd)
uabi_dup:
    push ebx
    mov eax, 37             ; UABI_DUP
    mov ebx, [esp + 8]      ; oldfd
    int 0x80
    pop ebx
    ret

global uabi_dup

; int uabi_kill(int pid, int sig)
uabi_kill:
    push ebx
    mov eax, 48             ; UABI_KILL
    mov ebx, [esp + 8]      ; pid
    mov ecx, [esp + 12]     ; sig
    int 0x80
    pop ebx
    ret

global uabi_kill

; int uabi_sigaction(int sig, void (*handler)(int))
uabi_sigaction:
    push ebx
    mov eax, 49             ; UABI_SIGACTION
    mov ebx, [esp + 8]      ; sig
    mov ecx, [esp + 12]     ; handler
    int 0x80
    pop ebx
    ret

global uabi_sigaction

; void uabi_sleep(uint32_t ms)
uabi_sleep:
    push ebx
    mov eax, 51             ; UABI_SLEEP
    mov ebx, [esp + 8]      ; ms
    int 0x80
    pop ebx
    ret

global uabi_sleep

; int uabi_devinfo(int index, uabi_devinfo_t *info)
uabi_devinfo:
    push ebx
    mov eax, 54             ; UABI_DEVINFO
    mov ebx, [esp + 8]      ; index
    mov ecx, [esp + 12]     ; info
    int 0x80
    pop ebx
    ret

global uabi_devinfo

; int uabi_mount(const char *device, const char *mountpoint)
uabi_mount:
    push ebx
    mov eax, 55             ; UABI_MOUNT
    mov ebx, [esp + 8]      ; device
    mov ecx, [esp + 12]     ; mountpoint
    int 0x80
    pop ebx
    ret

global uabi_mount

; int uabi_umount(const char *mountpoint)
uabi_umount:
    push ebx
    mov eax, 56             ; UABI_UMOUNT
    mov ebx, [esp + 8]      ; mountpoint
    int 0x80
    pop ebx
    ret

global uabi_umount
