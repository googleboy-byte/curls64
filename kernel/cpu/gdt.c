#include "gdt.h"
#include "../../libc/mem.h"
#include "../modules/drivers/screen.h"
#include "../core/task.h"

extern void gdt_flush(uint32_t);
extern void tss_flush(uint32_t selector);

gdt_entry_t gdt_entries[6 + MAX_CPU];
gdt_ptr_t   gdt_ptr;
tss_entry_t tss_entry;

cpu_local_t cpu_local[1]; // lets start with 1 cpu but we maintain the arch for multiple

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

static void write_tss(int32_t num, tss_entry_t *tss, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    gdt_set_gate(num, base, limit, 0x89, 0x00);

    memory_set((uint8_t*)tss, 0, sizeof(tss_entry_t));

    tss->ss0  = ss0;  // Kernel data segment
    tss->esp0 = esp0; // Kernel stack pointer

    // Here we set the cs, ss, ds, es, fs and gs entries in the TSS. These are actually
    // the selectors which are internal to the processor, and not the ones in the GDT.
    // However, they are setting up to the ring 3 selectors.
    tss->cs   = 0x0b; 
    tss->ss = tss->ds = tss->es = tss->fs = tss->gs = 0x13;
}

void init_gdt() {
    kprint("  - Setting up descriptors...\n");
    // gdt_ptr.limit = (sizeof(gdt_entry_t) * 6) - 1;
    // let's replace magic 6 with space for per cpu tss entries
    gdt_ptr.limit = (sizeof(gdt_entry_t) * (GDT_TSS_BASE + MAX_CPU)) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment
    
    // this became dangerous after updating write_tss to used *tss passed in argument
    // instead of fixed global tss
    // kprint("  - Initializing TSS...\n");
    // write_tss(5, 0x10, 0x0);

    kprint("  - Flushing GDT...\n");
    gdt_flush((uint32_t)&gdt_ptr);
    // kprint("  - Flushing TSS...\n");
    // tss_flush();

    // from now, i am keeping all tss to cpu init. let gdt be separate. 
    // cpu init still not called so won't break the system, yet, hopefully.
    kprint("  - GDT ready.\n");
}

void cpu_init(int cpu_id){
    cpu_local_t *cpu = &cpu_local[cpu_id];
    cpu->id = cpu_id;
    cpu->kstack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE, 1, NULL);
    cpu->kstack_top = cpu->kstack_base + KERNEL_STACK_SIZE;
    cpu->irq_depth = 0;

    write_tss(GDT_TSS_BASE + cpu_id, &cpu->tss, 0x10, cpu->kstack_top);
    tss_flush((GDT_TSS_BASE + cpu_id) << 3);
    // poison stack
    memory_set((uint8_t*)cpu->kstack_base, 0xCC, KERNEL_STACK_SIZE);
}

void set_kernel_stack(uint32_t stack) {
    cpu_local[0].tss.esp0 = stack;
}
