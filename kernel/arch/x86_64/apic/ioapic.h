#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>

void     ioapic_init(void);
void     ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id);
void     ioapic_mask_irq(uint8_t irq);
uint32_t ioapic_read(uint8_t reg);
uint32_t ioapic_read_irq_lo(uint8_t irq);
uint32_t ioapic_read_irq_hi(uint8_t irq);
#define MAX_REGISTERED_IRQS 16

typedef struct {
    uint8_t     irq;
    uint8_t     vector;
    uint8_t     dest_apic_id;
    uint8_t     should_be_masked;  // 1 if intentionally masked
    const char *name;
} irq_registration_t;

void irq_registry_add(uint8_t irq, uint8_t vector,
                      uint8_t dest, uint8_t masked,
                      const char *name);
int  irq_registry_verify_all(void);  // returns 1 if all correct, 0 otherwise
int  irq_registry_count(void);
irq_registration_t *irq_registry_get(int index);

#endif
