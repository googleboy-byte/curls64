#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Segment selectors */
#define KERNEL_CS 0x08

#ifndef ARCH_X86_64
/* How every interrupt gate (handler) is defined */
typedef struct {
    uint16_t low_offset; /* Lower 16 bits of handler function address */
    uint16_t sel; /* Kernel segment selector */
    uint8_t always0;
    uint8_t flags; 
    uint16_t high_offset; /* Higher 16 bits of handler function address */
} __attribute__((packed)) idt_gate_t ;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_register_t;
#else
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_gate_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_register_t;
#endif

#define IDT_ENTRIES 256
extern idt_gate_t idt[IDT_ENTRIES];
extern idt_register_t idt_reg;


/* Functions implemented in idt.c */
#ifndef ARCH_X86_64
void set_idt_gate(int n, uint32_t handler, uint8_t flags);
#else
void set_idt_gate(int n, uint64_t handler, uint8_t flags);
#endif
void set_idt();

#endif
