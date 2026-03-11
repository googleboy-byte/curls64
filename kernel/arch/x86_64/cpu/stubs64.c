#include <stdint.h>

// Stubs for symbols not yet ported or needed for IDT verification
void ktrace_event(int event, uint64_t data) {}
void ktrace_panic_snapshot(const char *msg) {}
void pipe_add_writer(void *pipe) {}
void pipe_add_reader(void *pipe) {}
void pipe_remove_writer(void *pipe) {}
void pipe_remove_reader(void *pipe) {}
void core_shutdown() {}
void core_reboot(int reason) {}
void first_user_entry_trampoline() {}
void print_test_menu() {}
void handle_test_command(int c) {}
void print_stress_menu() {}
void handle_stress_command(int c) {}
void run_core_test_v1(int c) {}

// Phase 6: Syscall dispatch stubs for unported subsystems
// These return -1 (error) to indicate "not implemented"
int pipe(int fds[2]) { return -1; }
int sys_getchar(void) { return -1; }
int sys_ps(void *buf, int count) { return -1; }
int sys_memstat(void *buf) { return -1; }
int fat32_vfs_mount(uint32_t dev, const char *mountpoint) { return -1; }

// VFS stubs (remaining)
int sys_readdir(const char *path, void *buf, int count) { return -1; }
int sys_getcwd(char *buf, int size) { return -1; }
int sys_chdir(const char *path) { return -1; }
int sys_stat(const char *path, void *buf) { return -1; }
