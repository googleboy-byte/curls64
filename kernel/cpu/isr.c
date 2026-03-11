#include "isr.h"
#include "idt.h"
#include "type.h"
#include "../../libc/string.h"
#include "timer.h"
#include "ports.h"
#include "../ktrace/ktrace.h"
#include "../core/task.h"
#include "../../include/kabi/kabi_v1.h"

isr_t interrupt_handlers[256];
volatile int irq_depth = 0;

#define PIC1_COMMAND 0x20
#define PIC2_COMMAND 0xA0
#define PIC_EOI      0x20

void send_eoi(uint32_t int_no) {
    uint8_t irq = int_no - 32;
    if (irq >= 8) {
        port_byte_out(PIC2_COMMAND, PIC_EOI);
    }
    port_byte_out(PIC1_COMMAND, PIC_EOI);
}

void isr_install() {
    for (int i = 0; i < 32; i++) {
        // We'll use a generic assembly stub for each, but for now we set them manually
        // in our existing assembly. 
        // These are already handled by our idt_gate calls in isr_install (legacy).
    }
    
    // Legacy mapping (simplified for brevity here, should match original)
    set_idt_gate(0, (uint32_t)isr0, 0x8E);
    set_idt_gate(1, (uint32_t)isr1, 0x8E);
    set_idt_gate(2, (uint32_t)isr2, 0x8E);
    set_idt_gate(3, (uint32_t)isr3, 0x8E);
    set_idt_gate(4, (uint32_t)isr4, 0x8E);
    set_idt_gate(5, (uint32_t)isr5, 0x8E);
    set_idt_gate(6, (uint32_t)isr6, 0x8E);
    set_idt_gate(7, (uint32_t)isr7, 0x8E);
    set_idt_gate(8, (uint32_t)isr8, 0x8E);
    set_idt_gate(9, (uint32_t)isr9, 0x8E);
    set_idt_gate(10, (uint32_t)isr10, 0x8E);
    set_idt_gate(11, (uint32_t)isr11, 0x8E);
    set_idt_gate(12, (uint32_t)isr12, 0x8E);
    set_idt_gate(13, (uint32_t)isr13, 0x8E);
    set_idt_gate(14, (uint32_t)isr14, 0x8E);
    set_idt_gate(15, (uint32_t)isr15, 0x8E);
    set_idt_gate(16, (uint32_t)isr16, 0x8E);
    set_idt_gate(17, (uint32_t)isr17, 0x8E);
    set_idt_gate(18, (uint32_t)isr18, 0x8E);
    set_idt_gate(19, (uint32_t)isr19, 0x8E);
    set_idt_gate(20, (uint32_t)isr20, 0x8E);
    set_idt_gate(21, (uint32_t)isr21, 0x8E);
    set_idt_gate(22, (uint32_t)isr22, 0x8E);
    set_idt_gate(23, (uint32_t)isr23, 0x8E);
    set_idt_gate(24, (uint32_t)isr24, 0x8E);
    set_idt_gate(25, (uint32_t)isr25, 0x8E);
    set_idt_gate(26, (uint32_t)isr26, 0x8E);
    set_idt_gate(27, (uint32_t)isr27, 0x8E);
    set_idt_gate(28, (uint32_t)isr28, 0x8E);
    set_idt_gate(29, (uint32_t)isr29, 0x8E);
    set_idt_gate(30, (uint32_t)isr30, 0x8E);
    set_idt_gate(31, (uint32_t)isr31, 0x8E);

    // Remap the PIC
    port_byte_out(0x20, 0x11);
    port_byte_out(0xA0, 0x11);
    port_byte_out(0x21, 0x20);
    port_byte_out(0xA1, 0x28);
    port_byte_out(0x21, 0x04);
    port_byte_out(0xA1, 0x02);
    port_byte_out(0x21, 0x01);
    port_byte_out(0xA1, 0x01);
    port_byte_out(0x21, 0x0);
    port_byte_out(0xA1, 0x0); 

    // Install the IRQs
    set_idt_gate(32, (uint32_t)irq0, 0x8E);
    set_idt_gate(33, (uint32_t)irq1, 0x8E);
    set_idt_gate(34, (uint32_t)irq2, 0x8E);
    set_idt_gate(35, (uint32_t)irq3, 0x8E);
    set_idt_gate(36, (uint32_t)irq4, 0x8E);
    set_idt_gate(37, (uint32_t)irq5, 0x8E);
    set_idt_gate(38, (uint32_t)irq6, 0x8E);
    set_idt_gate(39, (uint32_t)irq7, 0x8E);
    set_idt_gate(40, (uint32_t)irq8, 0x8E);
    set_idt_gate(41, (uint32_t)irq9, 0x8E);
    set_idt_gate(42, (uint32_t)irq10, 0x8E);
    set_idt_gate(43, (uint32_t)irq11, 0x8E);
    set_idt_gate(44, (uint32_t)irq12, 0x8E);
    set_idt_gate(45, (uint32_t)irq13, 0x8E);
    set_idt_gate(46, (uint32_t)irq14, 0x8E);
    set_idt_gate(47, (uint32_t)irq15, 0x8E);
    set_idt_gate(0x80, (uint32_t)isr80, 0xEE); // Syscall gate gets DPL 3

    set_idt(); // Load with ASM
}

char *exception_messages[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt"
    // ... truncated for brevity, but includes the common ones
};

void isr_handler(registers_t *r) {
    irq_depth++;
    assert_on_kstack(r);
    if (irq_depth > 2) panic("EXCESSIVE IRQ NESTING (ISR)");
    if (interrupt_handlers[r->int_no] != 0) {
        isr_t handler = interrupt_handlers[r->int_no];
        handler(r);
    } else {
        // If we have no handler, we can't do much without a console
        // For now, kernel will just hang or triple fault if panic is called
        panic("Unhandled ISR");
    }
    irq_depth--;
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

void irq_handler(registers_t *r) {
    irq_depth++;
    assert_on_kstack(r);
    if (irq_depth > 2) panic("EXCESSIVE IRQ NESTING (IRQ)");
    if (interrupt_handlers[r->int_no] != 0) {
        KTRACE1(KTRACE_IRQ_ENTER, r->int_no);
        isr_t handler = interrupt_handlers[r->int_no];
        handler(r);
        KTRACE1(KTRACE_IRQ_EXIT, r->int_no);
    }
    send_eoi(r->int_no);
    if (r->int_no == 32){
        task_switch(r);
    }
    irq_depth--;
}

void irq_install() {
    init_timer(50);
}

