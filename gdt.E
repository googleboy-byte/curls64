# 0 "kernel/arch/x86_64/cpu/gdt.c"
# 0 "<built-in>"
# 0 "<command-line>"
# 1 "kernel/arch/x86_64/cpu/gdt.c"
# 1 "kernel/arch/x86_64/cpu/gdt.h" 1



# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stdint.h" 1 3 4
# 14 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stdint.h" 3 4
# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stdint-gcc.h" 1 3 4
# 34 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stdint-gcc.h" 3 4

# 34 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stdint-gcc.h" 3 4
typedef signed char int8_t;


typedef short int int16_t;


typedef int int32_t;


typedef long int int64_t;


typedef unsigned char uint8_t;


typedef short unsigned int uint16_t;


typedef unsigned int uint32_t;


typedef long unsigned int uint64_t;




typedef signed char int_least8_t;
typedef short int int_least16_t;
typedef int int_least32_t;
typedef long int int_least64_t;
typedef unsigned char uint_least8_t;
typedef short unsigned int uint_least16_t;
typedef unsigned int uint_least32_t;
typedef long unsigned int uint_least64_t;



typedef signed char int_fast8_t;
typedef long int int_fast16_t;
typedef long int int_fast32_t;
typedef long int int_fast64_t;
typedef unsigned char uint_fast8_t;
typedef long unsigned int uint_fast16_t;
typedef long unsigned int uint_fast32_t;
typedef long unsigned int uint_fast64_t;




typedef long int intptr_t;


typedef long unsigned int uintptr_t;




typedef long int intmax_t;
typedef long unsigned int uintmax_t;
# 15 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stdint.h" 2 3 4
# 5 "kernel/arch/x86_64/cpu/gdt.h" 2
# 15 "kernel/arch/x86_64/cpu/gdt.h"

# 15 "kernel/arch/x86_64/cpu/gdt.h"
struct gdt_entry64_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));
typedef struct gdt_entry64_struct gdt_entry64_t;


struct gdt_tss_descriptor64_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));
typedef struct gdt_tss_descriptor64_struct gdt_tss_descriptor64_t;


struct gdt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;


struct tss64_entry_struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));
typedef struct tss64_entry_struct tss64_entry_t;

void init_gdt();
void cpu_init(int cpu_id);
void set_kernel_stack(uint64_t stack);
# 2 "kernel/arch/x86_64/cpu/gdt.c" 2
# 1 "kernel/arch/x86_64/cpu/../../../../libc/mem.h" 1




# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 160 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4

# 160 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4
typedef long int ptrdiff_t;
# 229 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4
typedef long unsigned int size_t;
# 344 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4
typedef int wchar_t;
# 440 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4
typedef struct {
  long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
  long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
# 451 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4
} max_align_t;
# 465 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 3 4
  typedef __typeof__(nullptr) nullptr_t;
# 6 "kernel/arch/x86_64/cpu/../../../../libc/mem.h" 2
# 1 "kernel/include/kernel/arch_types.h" 1




# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 6 "kernel/include/kernel/arch_types.h" 2
# 17 "kernel/include/kernel/arch_types.h"
    
# 17 "kernel/include/kernel/arch_types.h"
   typedef uint64_t virt_addr_t;
    typedef uint64_t phys_addr_t;
# 7 "kernel/arch/x86_64/cpu/../../../../libc/mem.h" 2

# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 9 "kernel/arch/x86_64/cpu/../../../../libc/mem.h" 2

void memory_copy(uint8_t *source, uint8_t *dest, size_t nbytes);
void memory_set(uint8_t *dest, uint8_t val, size_t len);
int memory_compare(uint8_t *s1, uint8_t *s2, int n);


void *kmalloc(size_t size, int align, phys_addr_t *phys_addr);
uint64_t kmalloc_int(size_t size, int align, phys_addr_t *phys_addr);
extern uintptr_t free_mem_addr;

void kfree(void *p);


typedef struct heap heap_t;
extern heap_t *kheap;
# 3 "kernel/arch/x86_64/cpu/gdt.c" 2
# 1 "kernel/arch/x86_64/cpu/../../../../libc/string.h" 1




# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 6 "kernel/arch/x86_64/cpu/../../../../libc/string.h" 2

void int_to_ascii(int n, char str[]);
void hex_to_ascii(uint64_t n, char str[]);
void hex64_to_ascii(uint64_t n, char str[]);
void reverse(char s[]);
int strlen(const char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(const char s1[], const char s2[]);
void strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
# 4 "kernel/arch/x86_64/cpu/gdt.c" 2
# 1 "kernel/arch/x86_64/cpu/../../../../libc/kheap.h" 1




# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 6 "kernel/arch/x86_64/cpu/../../../../libc/kheap.h" 2
# 23 "kernel/arch/x86_64/cpu/../../../../libc/kheap.h"
typedef struct {
    uint32_t magic;
    uint8_t is_hole;
    uint32_t size;
} header_t;

typedef struct {
    uint32_t magic;
    header_t *header;
} footer_t;



typedef void* type_t;
typedef int8_t (*lessthan_pred_t)(type_t,type_t);
typedef struct {
    type_t *array;
    uint32_t size;
    uint32_t max_size;
    lessthan_pred_t less_than;
} ordered_array_t;

typedef struct heap {
    ordered_array_t index;
    virt_addr_t start_address;
    virt_addr_t end_address;
    virt_addr_t max_address;
    uint8_t supervisor;
    uint8_t readonly;
} heap_t;




heap_t *create_heap(virt_addr_t start, virt_addr_t end, virt_addr_t max, uint8_t supervisor, uint8_t readonly);





void *alloc(size_t size, uint8_t page_align, heap_t *heap);





void free(void *p, heap_t *heap);
void expand(virt_addr_t new_size, heap_t *heap);
virt_addr_t contract(virt_addr_t new_size, heap_t *heap);

typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    virt_addr_t max_addr;
} heap_stats_t;

void get_heap_stats(heap_stats_t *stats);

void* lookup_ordered_array(uint32_t i, ordered_array_t *array);




uint64_t kmalloc_int(size_t size, int align, phys_addr_t *phys);
void kfree(void *p);
# 5 "kernel/arch/x86_64/cpu/gdt.c" 2
# 1 "kernel/arch/x86_64/cpu/../../../modules/drivers/screen.h" 1
# 16 "kernel/arch/x86_64/cpu/../../../modules/drivers/screen.h"
void clear_screen();
void kprint_at(const char *message, int col, int row);
void kprint(const char *message);
void kprint_backspace();
void set_cursor_position(int col, int row);
void screen_driver_init();
# 6 "kernel/arch/x86_64/cpu/gdt.c" 2
# 1 "kernel/arch/x86_64/cpu/../../../core/task.h" 1



       

# 1 "kernel/arch/x86_64/cpu/../../../core/../cpu/paging.h" 1



# 1 "kernel/arch/x86_64/cpu/../../../core/../cpu/isr.h" 1






extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void isr80();
extern void isr128();

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();
# 91 "kernel/arch/x86_64/cpu/../../../core/../cpu/isr.h"
typedef struct {



   uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
   uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
   uint64_t int_no, err_code;
   uint64_t rip, cs, rflags, rsp, ss;
} registers_t;
# 113 "kernel/arch/x86_64/cpu/../../../core/../cpu/isr.h"
static inline uint64_t irq_save() {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    asm volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
}


void isr_install();
void isr_handler(registers_t *r);
void irq_install();

typedef void (*isr_t)(registers_t*);
extern volatile int irq_depth;
void register_interrupt_handler(uint8_t n, isr_t handler);
# 5 "kernel/arch/x86_64/cpu/../../../core/../cpu/paging.h" 2
# 15 "kernel/arch/x86_64/cpu/../../../core/../cpu/paging.h"
# 1 "kernel/arch/x86_64/cpu/../../../core/../cpu/../arch/x86_64/mmu/mmu.h" 1
# 25 "kernel/arch/x86_64/cpu/../../../core/../cpu/../arch/x86_64/mmu/mmu.h"
typedef uint64_t mmu_entry_t;

typedef struct {
    mmu_entry_t entries[512];
} mmu_table_t;

typedef struct {
    phys_addr_t pml4_phys;
    mmu_table_t *pml4_virt;
} mmu_context_t;




void mmu_init(void);




int mmu_map_page(mmu_context_t *ctx, virt_addr_t virt, phys_addr_t phys, uint64_t flags);




mmu_entry_t *mmu_get_entry(mmu_context_t *ctx, virt_addr_t virt);




void mmu_unmap_page(mmu_context_t *ctx, virt_addr_t virt);




mmu_context_t *mmu_clone_user(mmu_context_t *src);




void mmu_switch(mmu_context_t *ctx);




extern int mmu_high_active;




static inline void mmu_invlpg(virt_addr_t addr) {
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}
# 16 "kernel/arch/x86_64/cpu/../../../core/../cpu/paging.h" 2

typedef mmu_entry_t page_t;
typedef mmu_context_t page_directory_t;
# 69 "kernel/arch/x86_64/cpu/../../../core/../cpu/paging.h"
void init_paging();




void switch_page_directory(page_directory_t *new);





page_t *get_page(virt_addr_t address, int make, page_directory_t *dir);




void page_fault(registers_t *regs);

void unmap_page(virt_addr_t address);
extern void copy_page_physical(phys_addr_t src, phys_addr_t dest);




page_directory_t *clone_page_directory(page_directory_t *src);
void free_page_directory(page_directory_t *dir);
void promote_to_user_table(page_directory_t *dir, virt_addr_t start_address, uint32_t size);

typedef struct {

    uint64_t total_frames;
    uint64_t used_frames;
    uint64_t free_frames;





} pmm_stats_t;

void get_pmm_stats(pmm_stats_t *stats);


uint32_t pmm_first_free();
void pmm_set_frame(uint32_t frame);
void pmm_clear_frame(uint32_t frame);
int pmm_test_frame(uint32_t frame);

extern void frame_add_ref(uint32_t frame);
void frame_remove_ref(uint32_t frame);
uint8_t frame_get_ref(uint32_t frame);

extern page_directory_t *kernel_directory;
extern int pmm_is_ready;
extern uint64_t total_frames;
void pmm_reserve_early_memory();
# 7 "kernel/arch/x86_64/cpu/../../../core/task.h" 2

# 1 "kernel/arch/x86_64/cpu/../../../core/../cpu/isr.h" 1
# 9 "kernel/arch/x86_64/cpu/../../../core/task.h" 2
# 1 "kernel/arch/x86_64/cpu/../../../core/vfs_core.h" 1




# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 6 "kernel/arch/x86_64/cpu/../../../core/vfs_core.h" 2
# 27 "kernel/arch/x86_64/cpu/../../../core/vfs_core.h"
struct fs_node;

typedef struct {
    struct fs_node *node;
    uint64_t offset;
    uint32_t flags;
    uint32_t refcount;
} file_t;

typedef uint64_t (*read_type_t)(struct fs_node*, uint64_t, uint64_t, uint8_t*);
typedef uint64_t (*write_type_t)(struct fs_node*, uint64_t, uint64_t, uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*, uint64_t);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*, char *name);
typedef void (*create_type_t)(struct fs_node*, char *name, uint16_t mask);
typedef void (*mkdir_type_t)(struct fs_node*, char *name, uint16_t mask);
typedef int (*unlink_type_t)(struct fs_node*, char *name);

typedef struct fs_node {
    char name[128];
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t inode;
    uint64_t length;
    void *impl;
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    create_type_t create;
    mkdir_type_t mkdir;
    unlink_type_t unlink;
    struct fs_node *ptr;
} fs_node_t;

# 1 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h" 1
# 17 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
# 1 "/usr/lib/gcc/x86_64-linux-gnu/15/include/stddef.h" 1 3 4
# 18 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h" 2
# 29 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
typedef void kabi_task_t;
typedef void kabi_fs_node_t;
typedef void kabi_page_directory_t;

typedef struct {
    size_t total_size;
    size_t used_size;
    size_t free_size;
    virt_addr_t max_addr;
} kabi_heap_stats_t;

typedef struct {
    uint64_t total_frames;
    uint64_t used_frames;
    uint64_t free_frames;
} kabi_pmm_stats_t;
# 60 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
typedef enum {
    REBOOT_REASON_ADMIN,
    REBOOT_REASON_PANIC,
    REBOOT_REASON_UPDATE
} reboot_reason_t;
# 73 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
void *kmalloc(size_t size, int align, phys_addr_t *phys);





void kfree(void *p);




void kabi_get_heap_stats(kabi_heap_stats_t *stats);




void kabi_get_pmm_stats(kabi_pmm_stats_t *stats);
# 98 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
int kabi_map_user_memory(virt_addr_t addr, size_t len, uint32_t flags);





int kabi_request_shutdown();





int kabi_request_reboot(reboot_reason_t reason);







void kabi_task_exit() __attribute__((noreturn));




void kabi_yield();






typedef int kabi_signal_t;
# 143 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
int kabi_task_signal(int pid, kabi_signal_t signal);







int kabi_sigaction(int sig, virt_addr_t handler_eip);





int kabi_spawn_process(virt_addr_t entry_point, virt_addr_t user_stack);
int kabi_sys_exec(const char *path);
int kabi_sys_execve(const char *path, char **argv);
int kabi_fork();



void kabi_ps();




void kabi_wait_for_children();






typedef struct {
    uint32_t id;
    virt_addr_t user_esp;
    virt_addr_t user_eip;
    virt_addr_t kernel_stack;
    uint32_t capabilities;
    uint32_t parent_id;
    uint64_t ticks;
    uint8_t state;
    uint8_t _padding[7];
} kabi_task_info_t;
# 197 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
typedef struct {
    void *current;
    void *start;
    int first;
} kabi_task_iter_t;





int kabi_task_iter_begin(kabi_task_iter_t *it);







int kabi_task_next(kabi_task_iter_t *it, kabi_task_info_t *info);





void kabi_jump_to_user_mode(virt_addr_t entry, virt_addr_t stack) __attribute__((noreturn));




void kabi_kill_all_children();



typedef struct {
    const char *name;
    uint32_t version;






    int (*pick_next)(kabi_task_t **out);

    void (*on_task_added)(kabi_task_t *task);
    void (*on_task_removed)(kabi_task_t *task);
} kabi_scheduler_ops_t;




void kabi_scheduler_register(kabi_scheduler_ops_t *ops);



typedef struct kabi_dirent {
    char name[128];
    uint32_t ino;
    uint64_t size;
    uint8_t type;
    uint8_t attr;
} kabi_dirent_t;






typedef struct kabi_fs_ops {
    uint64_t (*read)(kabi_fs_node_t* node, uint64_t offset, uint64_t size, uint8_t *buffer);
    uint64_t (*write)(kabi_fs_node_t* node, uint64_t offset, uint64_t size, uint8_t *buffer);
    void (*open)(kabi_fs_node_t* node);
    void (*close)(kabi_fs_node_t* node);
    kabi_dirent_t* (*readdir)(kabi_fs_node_t* node, uint64_t index);
    kabi_fs_node_t* (*finddir)(kabi_fs_node_t* node, char *name);
    void (*create)(kabi_fs_node_t* node, char *name, uint16_t mask);
    void (*mkdir)(kabi_fs_node_t* node, char *name, uint16_t mask);
    int (*unlink)(kabi_fs_node_t* node, char *name);
} kabi_fs_ops_t;

void kabi_vfs_register(kabi_fs_ops_t *ops, const char *mountpoint);


kabi_fs_node_t* kabi_vfs_get_root();
kabi_fs_node_t* kabi_vfs_resolve_path(const char *path);
uint64_t kabi_vfs_read(kabi_fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t kabi_vfs_write(kabi_fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
kabi_dirent_t* kabi_vfs_readdir(kabi_fs_node_t *node, uint64_t index);
kabi_fs_node_t* kabi_vfs_finddir(kabi_fs_node_t *node, char *name);



typedef void (*kabi_irq_handler_t)(void *regs);
void kabi_irq_register(uint8_t n, kabi_irq_handler_t handler);




void kprint(const char *c);
void kabi_get_line(char *buf);
void kabi_int_to_ascii(int n, char str[]);
void kabi_hex_to_ascii(uint64_t n, char str[]);
void kabi_clear_screen();
int kabi_block_read(uint32_t dev, uint64_t lba, uint8_t *buf);
int kabi_block_write(uint32_t dev, uint64_t lba, uint8_t *buf);




int kabi_get_device_count();





int kabi_get_device_name(int index, char *buf);





uint64_t kabi_get_device_size(int index);


uint64_t kabi_get_ticks();
# 341 "kernel/arch/x86_64/cpu/../../../core/../../include/kabi/kabi_v1.h"
int kabi_debug_enabled();
void kabi_set_debug(int enabled);
# 68 "kernel/arch/x86_64/cpu/../../../core/vfs_core.h" 2

void kabi_vfs_register(kabi_fs_ops_t *ops, const char *mountpoint);
void vfs_canonicalize_path(char *dest, const char *src);
fs_node_t *vfs_resolve_path(const char *path);

struct dirent {
    char name[128];
    uint64_t ino;
    uint64_t size;
    uint8_t type;
    uint8_t attr;
};

extern fs_node_t *fs_root;
extern fs_node_t *std_node;

void init_fs();


uint64_t read_fs(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
uint64_t write_fs(fs_node_t *node, uint64_t offset, uint64_t size, uint8_t *buffer);
void open_fs(fs_node_t *node, uint8_t read, uint8_t write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint64_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);


file_t *file_create(fs_node_t *node, uint32_t flags);
int open(const char *name, int flags);
int close(int fd);
int read(int fd, char *buf, int size);
int write(int fd, const char *buf, int size);
int seek(int fd, int offset, int whence);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int sys_mkdir(const char *path);
int sys_unlink(const char *path);
int sys_umount(const char *path);
# 10 "kernel/arch/x86_64/cpu/../../../core/task.h" 2
# 1 "kernel/arch/x86_64/cpu/../../../core/../cpu/gdt.h" 1
# 30 "kernel/arch/x86_64/cpu/../../../core/../cpu/gdt.h"
struct gdt_entry64_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));
typedef struct gdt_entry64_struct gdt_entry64_t;

struct gdt_tss_descriptor64_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));
typedef struct gdt_tss_descriptor64_struct gdt_tss_descriptor64_t;


typedef gdt_entry64_t gdt_entry_t;


struct gdt_ptr_struct {
    uint16_t limit;
    uintptr_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;
# 95 "kernel/arch/x86_64/cpu/../../../core/../cpu/gdt.h"
struct tss64_entry_struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));
typedef struct tss64_entry_struct tss64_entry_t;
typedef tss64_entry_t tss_entry_t;


void init_gdt();
void cpu_init(int cpu_id);

void set_kernel_stack(uint64_t stack);
# 11 "kernel/arch/x86_64/cpu/../../../core/task.h" 2
# 1 "kernel/arch/x86_64/cpu/../../../core/signal.h" 1
# 25 "kernel/arch/x86_64/cpu/../../../core/signal.h"
struct task_struct;
void task_deliver_signal(struct task_struct *t, int sig);






int task_send_signal(int pid, int sig);





void task_send_sigint_foreground(void);
# 53 "kernel/arch/x86_64/cpu/../../../core/signal.h"
void task_check_pending_signals(registers_t *regs);
# 12 "kernel/arch/x86_64/cpu/../../../core/task.h" 2
# 37 "kernel/arch/x86_64/cpu/../../../core/task.h"
typedef struct task_struct {
    uint32_t magic;
    uint32_t id;






    virt_addr_t user_esp;
    virt_addr_t user_eip;


    virt_addr_t kernel_stack;
    virt_addr_t kernel_stack_base;


    page_directory_t *page_directory;
    struct task_struct *parent;
    volatile uint8_t state;
    uint32_t capabilities;
    file_t *fd_table[32];
    char cwd[512];
    struct task_struct *next;


    uint32_t pending_signals;
    uint32_t signal_mask;
    virt_addr_t sigterm_handler;
    virt_addr_t saved_eip;
    virt_addr_t saved_esp;
    int in_signal;
    uint32_t sleep_until;
    struct task_struct *sleep_next;
    int exit_code;
    uint32_t ticks;
} task_t;

typedef struct cpu_local{
    uint32_t id;


    virt_addr_t kstack_base;
    virt_addr_t kstack_top;


    task_t *current;


    uint32_t irq_depth;
    tss_entry_t tss;





} cpu_local_t;


extern cpu_local_t cpu_local[1];


void init_tasking();


void task_switch(registers_t *regs);



task_t *create_kernel_task(void (*entry)(void));
void schedule(registers_t *r);


extern volatile task_t *current_task;
extern volatile task_t *task_list;




int sys_fork(registers_t *regs);
int fork();



int spawn_process(virt_addr_t entry_point, virt_addr_t user_stack);


void move_stack(void *new_stack_start, uint32_t size);


extern void jump_to_user_mode(virt_addr_t address, virt_addr_t stack) __attribute__((noreturn));


int getpid();


void ps();


extern volatile task_t *sleep_queue;
void sleepq_insert(task_t *t, uint32_t wake_tick);
void sleepq_remove(task_t *t);


void kill(int pid);


void kill_all_children();
int wait_for_children();
void reap_zombies();


void task_deliver_signal(struct task_struct *t, int sig);
int task_send_signal(int pid, int sig);
void task_send_sigint_foreground(void);

void panic(char *message);
void check_pid2_guard(const char *label);



typedef kabi_scheduler_ops_t scheduler_ops_t;
extern kabi_scheduler_ops_t *current_scheduler;
void kabi_scheduler_register(kabi_scheduler_ops_t *ops);
void set_scheduler(scheduler_ops_t *sched);


int sys_exec(const char *path);

extern uint32_t next_pid;

static inline void assert_on_kstack(registers_t *regs) {
    if (!current_task) return;

    uintptr_t addr = (uintptr_t)regs;


    if (addr >= current_task->kernel_stack_base && addr < current_task->kernel_stack) {
        return;
    }


    if (addr >= cpu_local[0].kstack_base && addr < cpu_local[0].kstack_top) {
        return;
    }

    panic("KERNEL STACK ESCAPE");
}

static inline void assert_on_cpu_stack(cpu_local_t *cpu) {
    uintptr_t esp;
    asm volatile("mov %%rsp, %0" : "=r"(esp));

    if (esp < cpu->kstack_base || esp >= cpu->kstack_top) {



        panic("CPU STACK ESCAPE");
    }
}
# 7 "kernel/arch/x86_64/cpu/gdt.c" 2
# 1 "kernel/arch/x86_64/cpu/../../../core/boot_info.h" 1

       



typedef struct boot_framebuffer_info {
    uint8_t present;
    uint8_t type;
    uint8_t bpp;
    uint8_t _pad0;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint64_t addr;
} boot_framebuffer_info_t;

typedef struct boot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t _pad;
} boot_mmap_entry_t;


typedef struct boot_mmap_info {
    uint32_t count;
    boot_mmap_entry_t entries[128];
} boot_mmap_info_t;

extern boot_framebuffer_info_t boot_fb_info;
extern boot_mmap_info_t boot_mmap_info;
# 8 "kernel/arch/x86_64/cpu/gdt.c" 2

extern void gdt_flush(uintptr_t);
extern void tss_flush(uint32_t selector);



gdt_entry64_t gdt_entries[6 + (1024 * 2)];
gdt_ptr_t gdt_ptr;


cpu_local_t cpu_local[1];

static void gdt_set_gate(int32_t num, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].base_low = 0;
    gdt_entries[num].base_middle = 0;
    gdt_entries[num].base_high = 0;
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access = access;
}

static void write_tss64(int32_t num, tss64_entry_t *tss) {
    uintptr_t base = (uintptr_t)tss;
    uint32_t limit = sizeof(tss64_entry_t) - 1;

    gdt_tss_descriptor64_t *desc = (gdt_tss_descriptor64_t *)&gdt_entries[num];

    desc->limit_low = (limit & 0xFFFF);
    desc->base_low = (base & 0xFFFF);
    desc->base_mid = (base >> 16) & 0xFF;
    desc->access = 0x89;
    desc->granularity = ((limit >> 16) & 0x0F);
    desc->base_high = (base >> 24) & 0xFF;
    desc->base_upper = (base >> 32) & 0xFFFFFFFF;
    desc->reserved = 0;

    memory_set((uint8_t*)tss, 0, sizeof(tss64_entry_t));
    tss->iomap_base = sizeof(tss64_entry_t);
}

void init_gdt() {
    kprint("  - [x64] Setting up GDT descriptors...\n");

    gdt_ptr.limit = (sizeof(gdt_entry64_t) * (6 + (1024 * 2))) - 1;
    gdt_ptr.base = (uintptr_t)&gdt_entries;

    memory_set((uint8_t*)&gdt_entries, 0, sizeof(gdt_entries));


    gdt_set_gate(0, 0, 0, 0);

    gdt_set_gate(1, 0xFFFFF, 0x9A, 0x20);

    gdt_set_gate(2, 0xFFFFF, 0x92, 0x00);

    gdt_set_gate(3, 0xFFFFF, 0xFA, 0x20);

    gdt_set_gate(4, 0xFFFFF, 0xF2, 0x00);

    gdt_set_gate(5, 0xFFFFF, 0xFA, 0xC0);

    kprint("  - [x64] GDT Base: ");
    char s[20]; hex64_to_ascii(gdt_ptr.base, s); kprint(s); kprint("\n");

    kprint("  - [x64] Flushing GDT...\n");
    gdt_flush((uintptr_t)&gdt_ptr);
    kprint("  - [x64] GDT reloaded.\n");
}

void cpu_init(int cpu_id) {
    kprint("  - [x64] CPU Init: ");
    char sid[10]; int_to_ascii(cpu_id, sid); kprint(sid); kprint("\n");

    if (cpu_id >= 1024) return;

    cpu_local_t *cpu = &cpu_local[cpu_id];
    cpu->id = cpu_id;

    cpu->kstack_base = (virt_addr_t)kmalloc(0x2000, 1, 
# 87 "kernel/arch/x86_64/cpu/gdt.c" 3 4
                                                                 ((void *)0)
# 87 "kernel/arch/x86_64/cpu/gdt.c"
                                                                     );

    cpu->kstack_top = cpu->kstack_base + 0x2000;
    cpu->irq_depth = 0;

    write_tss64(5 + (cpu_id * 2), &cpu->tss);


    cpu->tss.rsp0 = cpu->kstack_top;

    kprint("  - [x64] Loading TSS selector: ");
    int tss_sel = (5 + (cpu_id * 2)) << 3;
    char sel_s[10]; hex64_to_ascii(tss_sel, sel_s); kprint(sel_s); kprint("\n");

    tss_flush(tss_sel);

    memory_set((uint8_t*)cpu->kstack_base, 0xCC, 0x2000);
    kprint("  - [x64] CPU TSS loaded.\n");
}

void set_kernel_stack(uint64_t stack) {
    cpu_local[0].tss.rsp0 = stack;
}
