#include "gdt.h"
#include <cpu_local.h>
#include "../../libc/mem.h"
#include "../../libc/string.h"
#include "../../libc/kheap.h"
#include "../modules/drivers/screen.h"
#include "../core/task.h"

#ifndef ARCH_X86_64
extern void gdt_flush(uint32_t);
#else
extern void gdt_flush(uintptr_t);
#endif
extern void tss_flush(uint32_t selector);

#ifdef ARCH_X86_64
gdt_entry_t gdt_entries[5 + (MAX_CPU * 2)];
#else
gdt_entry_t gdt_entries[6 + MAX_CPU];
#endif
gdt_ptr_t   gdt_ptr;

cpu_local_t cpu_local[1]; 

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

#ifdef ARCH_X86_64
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
#else
static void write_tss(int32_t num, tss_entry_t *tss, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;

    gdt_set_gate(num, base, limit, 0x89, 0x00);

    memory_set((uint8_t*)tss, 0, sizeof(tss_entry_t));

    tss->ss0  = ss0;
    tss->esp0 = esp0;

    tss->cs   = 0x0b; 
    tss->ss = tss->ds = tss->es = tss->fs = tss->gs = 0x13;
}
#endif

void init_gdt() {
    kprint("  - Setting up GDT descriptors...\n");
#ifdef ARCH_X86_64
    gdt_ptr.limit = (sizeof(gdt_entry64_t) * (5 + (MAX_CPU * 2))) - 1;
    gdt_ptr.base  = (uintptr_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF); // Kernel Code: G=1, L=1
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0x8F); // Kernel Data: G=1, L=0
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF); // User Code: G=1, L=1
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0x8F); // User Data: G=1, L=0

    kprint("  - GDT Base: ");
    char s[20]; hex64_to_ascii(gdt_ptr.base, s); kprint(s); kprint("\n");
#else
    gdt_ptr.limit = (sizeof(gdt_entry_t) * (GDT_TSS_BASE + MAX_CPU)) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code segment
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); // User mode code segment
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); // User mode data segment
#endif

    kprint("  - Flushing GDT...\n");
    gdt_flush((uintptr_t)&gdt_ptr);
    kprint("  - GDT reloaded.\n");
}

void cpu_init(int cpu_id) {
    kprint("  - CPU Init: ");
    char sid[10]; int_to_ascii(cpu_id, sid); kprint(sid); kprint("\n");
    
    cpu_local_t *cpu = &cpu_local[cpu_id];
    cpu->id = cpu_id;

#ifdef ARCH_X86_64
    cpu->kstack_base = (virt_addr_t)kmalloc(KERNEL_STACK_SIZE, 1, NULL);
    kprint("  - KStack: ");
    char s[20]; hex64_to_ascii(cpu->kstack_base, s); kprint(s); kprint("\n");
#else
    cpu->kstack_base = (uint32_t)kmalloc(KERNEL_STACK_SIZE, 1, NULL);
#endif
    cpu->kstack_top = cpu->kstack_base + KERNEL_STACK_SIZE;
    cpu->_current = 0;
    cpu->_irq_depth = 0;

#ifdef ARCH_X86_64
    write_tss64(GDT_TSS_BASE + (cpu_id * 2), &cpu->tss);
    tss_flush((GDT_TSS_BASE + (cpu_id * 2)) << 3);
#else
    write_tss(GDT_TSS_BASE + cpu_id, &cpu->tss, 0x10, cpu->kstack_top);
    tss_flush((GDT_TSS_BASE + cpu_id) << 3);
#endif

    memory_set((uint8_t*)cpu->kstack_base, 0xCC, KERNEL_STACK_SIZE);
    kprint("  - CPU TSS loaded.\n");
}

#ifdef ARCH_X86_64
void set_kernel_stack(uint64_t stack) {
    cpu_local[0].tss.rsp0 = stack;
}
#else
void set_kernel_stack(uint32_t stack) {
    cpu_local[0].tss.esp0 = stack;
}
#endif
