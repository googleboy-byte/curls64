#ifndef ARCH_X86_64_GDT_H
#define ARCH_X86_64_GDT_H

#include <stdint.h>

#define GDT_NULL        0
#define GDT_KERNEL_CS  1
#define GDT_KERNEL_DS  2
#define GDT_USER_CS    3
#define GDT_USER_DS    4
#define GDT_TSS_BASE   5

// Standard 64-bit GDT Entry (8 bytes)
struct gdt_entry64_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));
typedef struct gdt_entry64_struct gdt_entry64_t;

// 64-bit TSS Descriptor (16 bytes in x64)
struct gdt_tss_descriptor64_struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));
typedef struct gdt_tss_descriptor64_struct gdt_tss_descriptor64_t;

// GDT Pointer
struct gdt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));
typedef struct gdt_ptr_struct gdt_ptr_t;

// x86_64 Task State Segment
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

#endif
