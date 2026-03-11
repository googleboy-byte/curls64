#include <stdint.h>
#include <kernel/arch_types.h>

// Stubs for symbols not yet ported or needed for IDT verification
void ktrace_event(int event, uint64_t data) {}
void ktrace_panic_snapshot(const char *msg) {}
int kabi_debug_enabled() { return 0; }
void vfs_init_standard_fds(void *task) {}
void vfs_close_all_fds(void *task) {}
void pipe_add_writer(void *pipe) {}
void pipe_add_reader(void *pipe) {}
void promote_to_user_table(void *dir, uint64_t start, uint64_t len) {}

// Paging stubs
void *clone_page_directory(void *src) { return 0; }
void free_page_directory(void *dir) {}

// Phase 6: Syscall dispatch stubs for unported subsystems
// These return -1 (error) to indicate "not implemented"
int sys_execve(const char *path, char **argv, void *regs) { return -1; }
int pipe(int fds[2]) { return -1; }
int dup2(int oldfd, int newfd) { return -1; }
int dup(int oldfd) { return -1; }
void get_line(char *buf) {}
void clear_screen(void) {}
int sys_getchar(void) { return -1; }
void set_cursor_position(int col, int row) {}
int sys_ps(void *buf, int count) { return -1; }
int sys_memstat(void *buf) { return -1; }
void kabi_set_debug(int enabled) {}
int sys_mkdir(const char *path) { return -1; }
int sys_unlink(const char *path) { return -1; }
int block_dev_get_count(void) { return 0; }
void *block_dev_get_by_index(int index) { return 0; }
int fat32_vfs_mount(uint32_t dev, const char *mountpoint) { return -1; }
int sys_umount(const char *path) { return -1; }

// VFS stubs
int open(const char *path, int flags) { return -1; }
int close(int fd) { return -1; }
int read(int fd, char *buf, int count) { return -1; }
int write(int fd, const char *buf, int count) { return -1; }
int seek(int fd, int offset, int whence) { return -1; }
int sys_readdir(const char *path, void *buf, int count) { return -1; }
int sys_getcwd(char *buf, int size) { return -1; }
int sys_chdir(const char *path) { return -1; }
int sys_stat(const char *path, void *buf) { return -1; }
