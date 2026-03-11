#include "shutdown.h"
#include "../../../include/module/module_abi_v1.h"
#include "screen.h"

/**
 * @brief Shutdown driver (Module)
 * Now refactored to use the capability-gated K-ABI request.
 */
void shutdown() {
    kprint("[DRIVER] Requesting system shutdown via K-ABI...\n");
    int res = kabi_request_shutdown();
    
    if (res != KABI_SUCCESS) {
        kprint("[DRIVER] Shutdown request denied (EPERM).\n");
    }
}

/* --- Module Registration --- */
kabi_module_t __kabi_module_shutdown = {
    .name           = "shutdown",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = NULL,
    .exit           = NULL,
    .description    = "ACPI/QEMU shutdown driver"
};
