#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* ISRs reserved for CPU exceptions */
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
extern void isr128(); /* 0x80 = 128 decimal, used for syscall gate */
/* IRQ definitions */
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

#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39
#define IRQ8 40
#define IRQ9 41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

/* Struct which aggregates many registers.
 * It matches exactly the pushes on interrupt.asm. From the bottom:
 * - Pushed by the processor automatically
 * - `push byte`s on the isr-specific code: error code, then int number
 * - All the registers by pusha
 * - `push eax` whose lower 16-bits contain DS
 */
#ifndef ARCH_X86_64
typedef struct {
   uint32_t ds; /* Data segment selector */
   uint32_t edi, esi, ebp, useless, ebx, edx, ecx, eax; /* Pushed by pusha. */
   uint32_t int_no, err_code; /* Interrupt number and error code (if applicable) */
   uint32_t eip, cs, eflags, esp, ss; /* Pushed by the processor automatically */
} registers_t;
#else
typedef struct {
   /* Segment registers (for 32-bit compatibility mode) */
   /* Order must match interrupt64.asm pops: pop gs, pop fs, pop es, pop ds */
   uint64_t gs, fs, es, ds;
   /* Pushed by interrupt64.asm in order: rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15
    * Stack grows down, so r15 (last pushed) is at lowest address.
    * Struct reads from lowest address upward: */
   uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
   uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
   uint64_t int_no, err_code;
   uint64_t rip, cs, rflags, rsp, ss;
} registers_t;
#endif

#ifndef ARCH_X86_64
static inline uint32_t irq_save() {
    uint32_t flags;
    asm volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags) {
    asm volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

static inline uint32_t read_cr2() {
    uint32_t val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}
#else
static inline uint64_t irq_save() {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    asm volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
}

static inline uint64_t read_cr2() {
    uint64_t val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}
#endif

void isr_install();
void isr_handler(registers_t *r);
void irq_install();

typedef void (*isr_t)(registers_t*);
extern volatile int irq_depth;
void register_interrupt_handler(uint8_t n, isr_t handler);

#endif
