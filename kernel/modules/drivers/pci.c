#include "pci.h"
#include "../../cpu/ports.h"
#include "../../../include/module/module_abi_v1.h"

/**
 * PCI Bus Scanner (Module)
 * 
 * Minimal PCI configuration space access using I/O mechanism #1:
 * - Port 0xCF8: CONFIG_ADDRESS (bus/device/function/register)
 * - Port 0xCFC: CONFIG_DATA (32-bit read/write)
 */

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1U << 31)
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)slot << 11)
                     | ((uint32_t)func << 8)
                     | (offset & 0xFC);
    port_long_out(PCI_CONFIG_ADDR, address);
    return port_long_in(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (1U << 31)
                     | ((uint32_t)bus << 16)
                     | ((uint32_t)slot << 11)
                     | ((uint32_t)func << 8)
                     | (offset & 0xFC);
    port_long_out(PCI_CONFIG_ADDR, address);
    port_long_out(PCI_CONFIG_DATA, val);
}

uint32_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar_num) {
    uint8_t offset = PCI_BAR0 + (bar_num * 4);
    uint32_t bar = pci_config_read(bus, slot, func, offset);
    if (bar & 1) return bar & 0xFFFFFFFC;
    else         return bar & 0xFFFFFFF0;
}

void pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_config_read(bus, slot, func, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_config_write(bus, slot, func, PCI_COMMAND, cmd);
}

void pci_enable_mem_space(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_config_read(bus, slot, func, PCI_COMMAND);
    cmd |= PCI_CMD_MEM_SPACE;
    pci_config_write(bus, slot, func, PCI_COMMAND, cmd);
}

int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t *out) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t reg0 = pci_config_read(bus, slot, func, 0x00);
                uint16_t vendor = reg0 & 0xFFFF;
                if (vendor == 0xFFFF) {
                    if (func == 0) break;
                    continue;
                }

                uint32_t reg2 = pci_config_read(bus, slot, func, 0x08);
                uint8_t cls = (reg2 >> 24) & 0xFF;
                uint8_t sub = (reg2 >> 16) & 0xFF;
                uint8_t pi  = (reg2 >> 8)  & 0xFF;

                if (cls == class_code && sub == subclass && pi == prog_if) {
                    if (out) {
                        out->bus       = bus;
                        out->slot      = slot;
                        out->func      = func;
                        out->vendor_id = vendor;
                        out->device_id = (reg0 >> 16) & 0xFFFF;
                        out->class_code = cls;
                        out->subclass  = sub;
                        out->prog_if   = pi;
                        uint32_t irq_reg = pci_config_read(bus, slot, func, PCI_IRQ_LINE);
                        out->irq_line = irq_reg & 0xFF;
                        out->found = 1;
                    }
                    return 1;
                }

                if (func == 0) {
                    uint32_t hdr = pci_config_read(bus, slot, func, 0x0C);
                    if (!((hdr >> 16) & 0x80)) break;
                }
            }
        }
    }
    if (out) out->found = 0;
    return 0;
}

/* --- Module Registration --- */
kabi_module_t __kabi_module_pci = {
    .name           = "pci",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = NULL,
    .exit           = NULL,
    .description    = "PCI configuration space bus scanner"
};
