#include "smp.h"
#include "trampoline_blob.h"
#include "../../../../libc/mem.h"
#include "../../../include/cpu_local.h"
#include "../acpi/acpi.h"
#include "../apic/lapic.h"
#include "../../../../libc/kheap.h"
 // removed vga_print.h
#include "../../../cpu/isr.h"

#define PML4_OFFSET  16
#define GDT_OFFSET   20
#define STACK_OFFSET 30
#define ENTRY_OFFSET 38
#define CPUID_OFFSET 46

extern unsigned char build_trampoline_bin[];
extern unsigned int build_trampoline_bin_len;

extern void int_to_ascii(int n, char *str);
extern void hex_to_ascii(uint32_t n, char *str);
extern void hex64_to_ascii(uint64_t n, char *str);
extern void kprint(const char *str);

volatile uint32_t ap_ready_flags = 0;
volatile int      smp_tasking_ready = 0;
static uint64_t   ap_trampoline_stacks[8] = {0}; // trampoline stack_top per AP

static smp_tlb_shootdown_t global_shootdown = {
    .addr = 0,
    .pending_mask = 0,
    .lock = SPINLOCK_INIT
};

static inline uint64_t get_cr3(void) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
extern void cpu_init(int cpu_id);
extern void lapic_init(void);

void smp_tlb_handler(registers_t *regs) {
    (void)regs;

    // Issue #2: CR3-aware shootdown filtering.
    // If the shootdown targets a user-space address (< 0xFFFF800000000000),
    // only invalidate if we're in the same address space (same CR3).
    // Kernel-space addresses are global across all address spaces.
    virt_addr_t shoot_addr = global_shootdown.addr;
    if (shoot_addr < 0xFFFF800000000000ULL) {
        uint64_t my_cr3 = get_cr3();
        if (my_cr3 != global_shootdown.target_cr3) {
            // Different address space — skip invlpg, just ack
            goto ack;
        }
    }

    // 1. Invalidate local TLB entry (raw arch, no recursion)
    asm volatile("invlpg (%0)" : : "r"(shoot_addr) : "memory");

ack:
    // 2. Ack completion (atomic clear bit)
    int my_id = get_cpu_local()->id;
    __sync_fetch_and_and(&global_shootdown.pending_mask, ~(1 << my_id));

    // 3. EOI to LAPIC
    lapic_write(0x0B0, 0); // LAPIC_EOI

    // NOTE: No need to neutralize task_switch_rsp here — it is now per-CPU
    // (read via GS base in assembly), so an AP can never steal the BSP's
    // pending task switch.
}

#define SHOOTDOWN_TIMEOUT 50000000ULL  // ~50M iterations, rough safety net

void smp_tlb_shootdown(virt_addr_t addr) {
    if (!smp_tasking_ready) {
        // Issue #1: Use raw invlpg directly — no dependency cycle through mmu_invlpg
        asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
        return;
    }

    uint64_t flags = spin_lock_irqsave(&global_shootdown.lock);
    
    global_shootdown.addr = addr;
    // Issue #2: Record the initiator's CR3 so receivers can filter by address space
    global_shootdown.target_cr3 = get_cr3();
    
    // Issue #4: mfence — guarantee page table writes are globally visible
    // before any remote CPU executes invlpg
    asm volatile("mfence" ::: "memory");
    
    // Broadcast to all but self
    uint32_t targets = ap_ready_flags | (1 << 0); // Include BSP
    targets &= ~(1 << get_cpu_local()->id);
    
    global_shootdown.pending_mask = targets;
    
    // All Excluding Self, Fixed delivery, Edge, Assert | Vector 0x41
    lapic_write(0x310, 0);
    lapic_write(0x300, 0x000C0000 | 0x41);
    
    // Issue #3: Timeout-guarded wait to prevent permanent deadlock
    uint64_t timeout = SHOOTDOWN_TIMEOUT;
    while (global_shootdown.pending_mask != 0) {
        asm volatile("pause");
        if (--timeout == 0) {
            char s[20];
            kprint("[SMP] SHOOTDOWN TIMEOUT! pending_mask=0x");
            hex_to_ascii(global_shootdown.pending_mask, s);
            kprint(s);
            kprint("\n");
            // Break out — better to continue with stale TLBs than deadlock
            break;
        }
    }
    
    spin_unlock_irqrestore(&global_shootdown.lock, flags);
}

// AP entry function called from assembly trampoline
void ap_entry(int cpu_id) {
    kprint("AP inside C!\n");
    *((volatile uint16_t*)0xffff8000000b8000 + (cpu_id * 80)) = 0x0F00 | ('0' + cpu_id);
    // 1. Initialize this AP's cpu_local
    cpu_init(cpu_id);

    cpu_local_t *me = get_cpu_local();
    char s[16];
    kprint("[AP"); int_to_ascii(cpu_id, s); kprint(s);
    kprint("] get_cpu_local() = 0x"); hex64_to_ascii((uint64_t)me, s); kprint(s);
    kprint(", id="); int_to_ascii(me->id, s); kprint(s); kprint("\n");

    // 2. Initialize LAPIC on this AP
    lapic_enable_via_msr(0xFEE00000);

    // Enable LAPIC (Spurious register)
    lapic_write(0xF0, 0x100 | 0xFF);

    // 3. Signal BSP that this AP is ready
    ap_ready_flags |= (1 << cpu_id);

    // 4. Spin-wait until BSP signals tasking is ready
    while (!smp_tasking_ready) {
        asm volatile("pause");
    }

    // Assign this AP's dedicated idle task
    extern task_t *get_idle_task_for_cpu(int cpu_id);
    get_cpu_local()->_current = get_idle_task_for_cpu(cpu_id);
    asm volatile("" ::: "memory"); // Compiler barrier: ensure _current is written

    // Now safe to start timer — ready_queue exists
    lapic_timer_start_ap();

    // 5. Enable interrupts
    extern void set_idt(void);
    set_idt();
    asm volatile("sti");

    // 6. Enter scheduler idle loop
    while (1) {
        asm volatile("hlt");
    }
}

extern mmu_context_t *kernel_directory;
extern void pmm_clear_frame(uint32_t frame);
#define TRAMPOLINE_PHYS 0x70000ULL
#define TRAMPOLINE_VIRT (PHYSMAP_BASE + TRAMPOLINE_PHYS)
#define BREADCRUMB_PHYS 0x6000ULL
#define BREADCRUMB_VIRT (PHYSMAP_BASE + BREADCRUMB_PHYS)

void smp_copy_trampoline(void) {
    memory_copy(build_trampoline_bin, (uint8_t*)TRAMPOLINE_VIRT, build_trampoline_bin_len);

    uint8_t *check = (uint8_t*)TRAMPOLINE_VIRT;
    char s[16];
    kprint("[SMP] Trampoline[0..3]: ");
    hex_to_ascii(check[0], s); kprint(s); kprint(" ");
    hex_to_ascii(check[1], s); kprint(s); kprint(" ");
    hex_to_ascii(check[2], s); kprint(s); kprint(" ");
    hex_to_ascii(check[3], s); kprint(s); kprint("\n");
}

void lapic_send_ipi(uint8_t dest, uint32_t cmd) {
    // Step 1: Write destination to ICR_HIGH
    // Destination APIC ID goes in bits 31:24
    lapic_write(LAPIC_ICR_HIGH, (uint32_t)dest << 24);
    
    // Step 2: Write command to ICR_LOW — this FIRES the IPI
    lapic_write(LAPIC_ICR_LOW, cmd);
    
    // Step 3: Wait for delivery (bit 12 = delivery status)
    int timeout = 100000;
    while ((lapic_read(LAPIC_ICR_LOW) & (1 << 12)) && --timeout);
    if (!timeout) {
        kprint("[LAPIC] IPI delivery timeout to APIC ");
        char b[16];
        int_to_ascii(dest, b); kprint(b);
        kprint("\n");
    }
}

void smp_start_aps(void) {
    smp_copy_trampoline();
    
    smp_info_t *info = acpi_get_smp_info();

    extern gdt_ptr_t gdt_ptr;

    kprint("[SMP] Starting APs...\n");

    // Register TLB shootdown handler
    register_interrupt_handler(0x41, smp_tlb_handler);

    uint8_t *t = (uint8_t*)TRAMPOLINE_VIRT;
    kprint("[SMP] Trampoline bytes at 0x70000:\n");
    for (int i = 0; i < 32; i++) {
        char h[3];
        h[0] = "0123456789ABCDEF"[(t[i] >> 4) & 0xF];
        h[1] = "0123456789ABCDEF"[t[i] & 0xF];
        h[2] = 0;
        kprint(h); kprint(" ");
        if ((i + 1) % 16 == 0) kprint("\n");
    }

    for (int i = 0; i < info->ap_count; i++) {
        uint8_t apic_id = info->ap_apic_ids[i];
        int     cpu_id  = i + 1;  // BSP is 0

        void *stack = kmalloc(8192, 4096, 0);
        uint64_t stack_top = (uint64_t)stack + 8192;

        *(uint32_t*)(TRAMPOLINE_VIRT + PML4_OFFSET)    = (uint32_t)get_cr3();
        *(uint16_t*)(TRAMPOLINE_VIRT + GDT_OFFSET)     = gdt_ptr.limit;
        *(uint64_t*)(TRAMPOLINE_VIRT + GDT_OFFSET + 2) = gdt_ptr.base;
        *(uint64_t*)(TRAMPOLINE_VIRT + STACK_OFFSET)   = stack_top;
        *(uint64_t*)(TRAMPOLINE_VIRT + ENTRY_OFFSET)   = (uint64_t)ap_entry;
        *(uint32_t*)(TRAMPOLINE_VIRT + CPUID_OFFSET)   = cpu_id;

        asm volatile("mfence" ::: "memory");
        *((volatile uint16_t*)BREADCRUMB_VIRT) = 0x0000; // Reset breadcrumb

        // INIT Assert
        lapic_send_ipi(apic_id, 0x00004500);
        for(volatile int d=0; d<1000000; d++); // delay
        // INIT Deassert (Level = 0, Trigger = 1 -> 0x8500)
        lapic_send_ipi(apic_id, 0x00008500);
        for(volatile int d=0; d<1000000; d++); // delay

        // SIPI vec 0x70
        lapic_send_ipi(apic_id, 0x00004670);
        for(volatile int d=0; d<10000; d++); // delay

        // Second SIPI vec 0x70
        lapic_send_ipi(apic_id, 0x00004670);
        for(volatile int d=0; d<100000; d++); // delay

        int timeout = 10000000;
        while (!(ap_ready_flags & (1 << cpu_id)) && timeout-- > 0) {
            if (timeout % 1000000 == 0) {
                // optional: add mfence if needed, but volatile should handle it
                asm volatile("mfence" ::: "memory");
            }
            asm volatile("pause");
        }

        uint16_t *breadcrumb = (uint16_t*)BREADCRUMB_VIRT;
        kprint("[SMP] AP breadcrumb: ");
        char b[16]; int_to_ascii(*breadcrumb, b); kprint(b); kprint(" (expect CAFE)\n");

        if (ap_ready_flags & (1 << cpu_id)) {
            ap_trampoline_stacks[cpu_id] = stack_top;
            char b[16];
            kprint("[SMP] AP ");
            int_to_ascii(cpu_id, b); kprint(b);
            kprint(" (APIC ");
            int_to_ascii(apic_id, b); kprint(b);
            kprint(") online\n");
        } else {
            char b[16];
            kprint("[SMP] AP ");
            int_to_ascii(cpu_id, b); kprint(b);
            kprint(" TIMEOUT\n");
        }
    }
}

extern task_t *create_ap_idle_task(int cpu_id, uint64_t stack_top);

void smp_signal_ready(void) {
    smp_info_t *info = acpi_get_smp_info();
    for (int i = 0; i < info->ap_count; i++) {
        int cpu_id = i + 1;
        if (ap_ready_flags & (1 << cpu_id)) {
            uint64_t stack_top = ap_trampoline_stacks[cpu_id];
            create_ap_idle_task(cpu_id, stack_top);
        }
    }
    // Ensure all idle task writes are visible to APs before they unblock
    asm volatile("mfence" ::: "memory");
    smp_tasking_ready = 1;
    kprint("[SMP] Tasking ready -- APs cleared to start timers\n");
}

