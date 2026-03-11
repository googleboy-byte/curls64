#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/**
 * PCI Configuration Space Access (Module)
 * Minimal PCI bus enumeration via I/O ports 0xCF8/0xCFC.
 */

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

/* Standard PCI config register offsets */
#define PCI_VENDOR_ID    0x00
#define PCI_DEVICE_ID    0x02
#define PCI_COMMAND      0x04
#define PCI_STATUS       0x06
#define PCI_REVISION     0x08
#define PCI_PROG_IF      0x09
#define PCI_SUBCLASS     0x0A
#define PCI_CLASS        0x0B
#define PCI_HEADER_TYPE  0x0E
#define PCI_BAR0         0x10
#define PCI_BAR1         0x14
#define PCI_BAR2         0x18
#define PCI_BAR3         0x1C
#define PCI_BAR4         0x20
#define PCI_BAR5         0x24
#define PCI_IRQ_LINE     0x3C

/* PCI command register bits */
#define PCI_CMD_IO_SPACE       (1 << 0)
#define PCI_CMD_MEM_SPACE      (1 << 1)
#define PCI_CMD_BUS_MASTER     (1 << 2)
#define PCI_CMD_INT_DISABLE    (1 << 10)

/* PCI device location */
typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq_line;
    int      found;
} pci_device_t;

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_num);
void pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func);
void pci_enable_mem_space(uint8_t bus, uint8_t slot, uint8_t func);
int  pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t *out);

#endif
