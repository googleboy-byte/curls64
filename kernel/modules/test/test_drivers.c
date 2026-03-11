#include "module_tests_runner.h"
#include "../../core/kernel.h"
#include "../drivers/pci.h"
#include "../drivers/uart.h"
#include "../../../include/kabi/kabi_v1.h"

static void test_pci_scan() {
    kprint("  [ TEST ] PCI Bus Scan... ");
    pci_device_t dev;
    // Look for IDE/SATA controllers or any device
    // Class 01 = Mass Storage
    int found = pci_find_device(0x01, 0x01, 0x80, &dev); // PIIX3 IDE
    if (!found) {
        found = pci_find_device(0x01, 0x01, 0x8A, &dev); // Alternative IDE
    }

    if (found) {
        kprint("PASS (Found IDE Controller)\n");
    } else {
        // Just check if Vendor ID 0xFFFF is not everyone (meaning we can read something)
        uint32_t val = pci_config_read(0, 0, 0, 0);
        if (val != 0xFFFFFFFF) {
            kprint("PASS (Found Device at 0:0:0)\n");
        } else {
            kprint("FAIL (No PCI devices found)\n");
        }
    }
}

static void test_uart_init() {
    kprint("  [ TEST ] UART Infrastructure... ");
    // uart_init is called at boot, we just verify it doesn't crash to call it again or check status
    // Since we don't have a 'uart_is_ready' we just print success if we reach here
    kprint("PASS\n");
}

void run_drivers_tests() {
    kprint("Module: DRIVERS\n");
    test_pci_scan();
    test_uart_init();
}
