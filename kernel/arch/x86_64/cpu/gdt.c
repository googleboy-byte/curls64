#include "gdt.h"
#include <cpu_local.h>
#include <cpu_local_offsets.h>
#include "../../../../libc/mem.h"
#include "../../../../libc/string.h"
#include "../../../../libc/kheap.h"
#include "../../../modules/drivers/screen.h"
#include "../../../core/task.h"
#include "../../../core/boot_info.h"

extern void gdt_flush(uintptr_t);
extern void tss_flush(uint32_t selector);

// 7 standard entries (Null, KCode, KData, UCode64, UData, UCode32, UData32) 
// + 2 slots per TSS (16-byte descriptors)
gdt_entry64_t gdt_entries[7 + (MAX_CPU * 2)];
gdt_ptr_t     gdt_ptr;

#define MAX_SMP_CPUS 8
cpu_local_t cpu_local[MAX_SMP_CPUS];

static void gdt_set_gate(int32_t num, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].base_low    = 0;
    gdt_entries[num].base_middle = 0;
    gdt_entries[num].base_high   = 0;
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

static void write_tss64(int32_t num, tss64_entry_t *tss) {
    uintptr_t base = (uintptr_t)tss;
    uint32_t limit = sizeof(tss64_entry_t) - 1;

    gdt_tss_descriptor64_t *desc = (gdt_tss_descriptor64_t *)&gdt_entries[num];
    
    desc->limit_low = (limit & 0xFFFF);
    desc->base_low = (base & 0xFFFF);
    desc->base_mid = (base >> 16) & 0xFF;
    desc->access = 0x89; // Present, Executable, Accessible from Ring 0
    desc->granularity = ((limit >> 16) & 0x0F);
    desc->base_high = (base >> 24) & 0xFF;
    desc->base_upper = (base >> 32) & 0xFFFFFFFF;
    desc->reserved = 0;

    memory_set((uint8_t*)tss, 0, sizeof(tss64_entry_t));
    tss->iomap_base = sizeof(tss64_entry_t);
}

void init_gdt() {
    kprint("  - [x64] Setting up GDT descriptors...\n");
    
    gdt_ptr.limit = (sizeof(gdt_entry64_t) * (7 + (MAX_CPU * 2))) - 1;
    gdt_ptr.base  = (uintptr_t)&gdt_entries;

    memory_set((uint8_t*)&gdt_entries, 0, sizeof(gdt_entries));
    memory_set((uint8_t*)cpu_local, 0, sizeof(cpu_local));

    // Null segment
    gdt_set_gate(0, 0, 0, 0);
    // Kernel Code: Present, Ring 0, Code, Exec/Read (0x9A), Long Mode (0x20)
    gdt_set_gate(1, 0xFFFFF, 0x9A, 0x20); 
    // Kernel Data: Present, Ring 0, Data, Read/Write (0x92)
    gdt_set_gate(2, 0xFFFFF, 0x92, 0x00);
    // User Code: Present, Ring 3, Code, Exec/Read (0xFA), Long Mode (0x20)
    gdt_set_gate(3, 0xFFFFF, 0xFA, 0x20);
    // User Data: Present, Ring 3, Data, Read/Write (0xF2)
    gdt_set_gate(4, 0xFFFFF, 0xF2, 0x00);
    // User Code 32-bit Compat: Present, Ring 3, Code, Exec/Read (0xFA), D=1/L=0 (0x40 + G=1 -> 0xC0)
    gdt_set_gate(5, 0xFFFFF, 0xFA, 0xC0);
    // User Data 32-bit Compat: Present, Ring 3, Data, Read/Write (0xF2), D=1/L=0 (0x40 + G=1 -> 0xC0)
    gdt_set_gate(6, 0xFFFFF, 0xF2, 0xC0);

    kprint("  - [x64] GDT Base: ");
    char s[20]; hex64_to_ascii(gdt_ptr.base, s); kprint(s); kprint("\n");

    kprint("  - [x64] Flushing GDT...\n");
    gdt_flush((uintptr_t)&gdt_ptr);
    kprint("  - [x64] GDT reloaded.\n");
}

void cpu_init(int cpu_id) {
    kprint("  - [x64] CPU Init: ");
    char sid[10]; int_to_ascii(cpu_id, sid); kprint(sid); kprint("\n");
    
    if (cpu_id >= MAX_CPU) return;

    cpu_local_t *cpu = &cpu_local[cpu_id];
    cpu->id = cpu_id;
    cpu->_current = 0;
    cpu->_irq_depth = 0;
    cpu->timer_ticks = 0;
    cpu->kstack_base = (virt_addr_t)kmalloc(8192, 4096, 0);
    cpu->kstack_top = cpu->kstack_base + 8192;
    // Set kernel stack for this CPU's TSS
    cpu->tss.rsp0 = cpu->kstack_top;
    cpu->_task_switch_rsp = 0; // No pending task switch at boot
    
    write_tss64(GDT_TSS_BASE + (cpu_id * 2), &cpu->tss);
    
    // Set initial kernel stack
    cpu->tss.rsp0 = cpu->kstack_top;

    kprint("  - [x64] Loading TSS selector: ");
    int tss_sel = (GDT_TSS_BASE + (cpu_id * 2)) << 3;
    char sel_s[10]; hex64_to_ascii(tss_sel, sel_s); kprint(sel_s); kprint("\n");
    
    tss_flush(tss_sel);

    memory_set((uint8_t*)cpu->kstack_base, 0xCC, KERNEL_STACK_SIZE);
    
    // Store pointer to this CPU's local struct in GS base
    write_gs_base((uint64_t)&cpu_local[cpu_id]);
    char s[16];
    kprint("[CPU"); int_to_ascii(cpu_id, s); kprint(s);
    kprint("] GS base set to cpu_local @ 0x");
    hex64_to_ascii((uint64_t)&cpu_local[cpu_id], s); kprint(s); kprint("\n");
            
    kprint("  - [x64] CPU TSS loaded.\n");
}

void set_kernel_stack(uint64_t stack) {
    get_cpu_local()->tss.rsp0 = stack;
}
