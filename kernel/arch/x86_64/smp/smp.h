#include <stdint.h>
#include <kernel/arch_types.h>
#include "../../../cpu/isr.h"
#include "../../../include/spinlock.h"

typedef struct {
    volatile virt_addr_t addr;
    volatile uint64_t    target_cr3;   // Issue #2: address space context
    volatile uint32_t    pending_mask;
    spinlock_t           lock;
} smp_tlb_shootdown_t;

void smp_tlb_shootdown(virt_addr_t addr);
void smp_tlb_handler(registers_t *regs);

void smp_copy_trampoline(void);
void smp_start_aps(void);
void ap_entry(int cpu_id);
void lapic_send_ipi(uint8_t dest, uint32_t cmd);

extern volatile uint32_t ap_ready_flags;
extern volatile int      smp_tasking_ready;
