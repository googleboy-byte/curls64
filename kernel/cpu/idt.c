#include "idt.h"
#include "type.h"

idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;

#ifndef ARCH_X86_64
void set_idt_gate(int n, uint32_t handler, uint8_t flags) {
    idt[n].low_offset = low_16(handler);
    idt[n].sel = KERNEL_CS;
    idt[n].always0 = 0;
    idt[n].flags = flags; 
    idt[n].high_offset = high_16(handler);
}

void set_idt() {
    idt_reg.base = (uint32_t) &idt;
    idt_reg.limit = IDT_ENTRIES * sizeof(idt_gate_t) - 1;
    /* Don't make the mistake of loading &idt -- always load &idt_reg */
    asm volatile("lidtl (%0)" : : "r" (&idt_reg));
}
#else
void set_idt_gate(int n, uint64_t handler, uint8_t flags) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = KERNEL_CS;
    idt[n].ist = 0;
    idt[n].flags = flags;
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].reserved = 0;
}

void set_idt() {
    idt_reg.base = (uint64_t) &idt;
    idt_reg.limit = IDT_ENTRIES * sizeof(idt_gate_t) - 1;
    asm volatile("lidt (%0)" : : "r" (&idt_reg));
}
#endif
