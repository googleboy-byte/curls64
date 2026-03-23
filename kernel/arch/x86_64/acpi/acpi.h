#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

typedef struct {
    uint32_t lapic_base;        // Local APIC MMIO base
    uint32_t ioapic_base;       // IO APIC MMIO base
    int      ap_count;          // number of APs (excludes BSP)
    uint8_t  ap_apic_ids[16];   // APIC IDs of APs
    uint8_t  bsp_apic_id;       // BSP APIC ID
} smp_info_t;

smp_info_t *acpi_get_smp_info(void);
void acpi_parse(void);

#endif // ACPI_H
