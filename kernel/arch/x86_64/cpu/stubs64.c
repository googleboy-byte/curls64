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

// These need real implementations eventually, but for IDT verification
// we might be able to get away with stubs if we don't fork/spawn.
void *clone_page_directory(void *src) { return 0; }
void free_page_directory(void *dir) {}

// mb2_boot_stack symbols from assembly
extern uint8_t mb2_boot_stack;
extern uint8_t mb2_boot_stack_top;
// We actually have them in assembly, so we don't need stubs if they are linked.
// But they were reported as undefined references. 
// This means Multiboot2 entry might not be exporting them or naming them differently.
