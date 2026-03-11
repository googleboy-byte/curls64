#include "ehci.h"
#include "usb_core.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "../../cpu/paging.h"

/**
 * EHCI USB 2.0 Host Controller Driver (Module)
 *
 * Hardware-specific driver that communicates with the EHCI controller
 * via MMIO registers. Exposes a transfer API consumed by USB core.
 *
 * Architecture:
 *   [Mass Storage]  <-- class driver (future)
 *   [USB Core]      <-- protocol layer
 *   [EHCI Driver]   <-- you are here
 *   [PCI Scanner]   <-- bus enumeration
 */

// ============================================================================
// Global State
// ============================================================================

static ehci_controller_t ehci;

// ============================================================================
// MMIO Helpers
// ============================================================================

static uint32_t ehci_cap_read32(uint32_t offset) {
    return *(volatile uint32_t *)((uint8_t *)ehci.cap_base + offset);
}

static uint32_t ehci_op_read32(uint32_t offset) {
    return *(volatile uint32_t *)((uint8_t *)ehci.op_base + offset);
}

static void ehci_op_write32(uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)((uint8_t *)ehci.op_base + offset) = val;
}

// ============================================================================
// Delay
// ============================================================================

static void ehci_delay(uint32_t ms) {
    volatile uint32_t count = ms * 10000;
    while (count--) asm volatile("nop");
}

// ============================================================================
// BIOS Handoff
// ============================================================================

static void ehci_bios_handoff(void) {
    uint32_t eecp = HCCPARAMS_EECP(ehci.hcc_params);
    if (eecp == 0) return;

    uint32_t legsup = pci_config_read(ehci.pci_dev.bus, ehci.pci_dev.slot,
                                       ehci.pci_dev.func, eecp);
    if (legsup & USBLEGSUP_BIOS_OWNED) {
        legsup |= USBLEGSUP_OS_OWNED;
        pci_config_write(ehci.pci_dev.bus, ehci.pci_dev.slot,
                         ehci.pci_dev.func, eecp, legsup);

        int timeout = 100;
        while (timeout-- > 0) {
            legsup = pci_config_read(ehci.pci_dev.bus, ehci.pci_dev.slot,
                                     ehci.pci_dev.func, eecp);
            if (!(legsup & USBLEGSUP_BIOS_OWNED)) break;
            ehci_delay(10);
        }
        if (legsup & USBLEGSUP_BIOS_OWNED) {
            legsup &= ~USBLEGSUP_BIOS_OWNED;
            legsup |= USBLEGSUP_OS_OWNED;
            pci_config_write(ehci.pci_dev.bus, ehci.pci_dev.slot,
                             ehci.pci_dev.func, eecp, legsup);
        }
    }
}

// ============================================================================
// Controller Reset
// ============================================================================

static int ehci_reset(void) {
    uint32_t cmd = ehci_op_read32(EHCI_OP_USBCMD);
    cmd &= ~USBCMD_RUN_STOP;
    ehci_op_write32(EHCI_OP_USBCMD, cmd);

    int timeout = 100;
    while (timeout-- > 0) {
        if (ehci_op_read32(EHCI_OP_USBSTS) & USBSTS_HALTED) break;
        ehci_delay(1);
    }

    cmd = ehci_op_read32(EHCI_OP_USBCMD);
    cmd |= USBCMD_HCRESET;
    ehci_op_write32(EHCI_OP_USBCMD, cmd);

    timeout = 100;
    while (timeout-- > 0) {
        cmd = ehci_op_read32(EHCI_OP_USBCMD);
        if (!(cmd & USBCMD_HCRESET)) break;
        ehci_delay(1);
    }
    if (cmd & USBCMD_HCRESET) return KABI_EIO;

    kprint("[EHCI] Controller reset complete\n");
    return KABI_SUCCESS;
}

// ============================================================================
// MMIO Mapping
// ============================================================================

static volatile uint32_t *ehci_map_mmio(uint32_t phys_addr, uint32_t size) {
    uint32_t virt = PHYSMAP_BASE + phys_addr;
    for (uint32_t offset = 0; offset < size; offset += 0x1000) {
        page_t *page = get_page(virt + offset, 1, kernel_directory);
        if (!page->present) {
            page->frame   = (phys_addr + offset) >> 12;
            page->present = 1;
            page->rw      = 1;
            page->user    = 0;
            page->pcd     = 1;
            page->pwt     = 1;
        }
    }
    return (volatile uint32_t *)virt;
}

// ============================================================================
// Async Schedule (Control/Bulk Transfers)
// ============================================================================

static int ehci_setup_async(void) {
    /* Allocate the head QH for the async circular list.
     * This QH is always present and points to itself (empty schedule).
     * Real transfer QHs get inserted after it. */
    ehci.async_qh = (ehci_qh_t *)kmalloc(sizeof(ehci_qh_t), 1, &ehci.async_qh_phys);
    if (!ehci.async_qh) return KABI_ENOMEM;
    memory_set((uint8_t *)ehci.async_qh, 0, sizeof(ehci_qh_t));

    /* Point to self — empty circular list */
    ehci.async_qh->next_qh = ehci.async_qh_phys | QH_LINK_TYPE_QH;
    ehci.async_qh->ep_chars = QH_EP_HRECLAIM | QH_EP_SPEED_HIGH |
                               QH_EP_MAX_PACKET(64) | QH_EP_NAK_RELOAD(4);
    ehci.async_qh->ep_caps = QH_CAP_MULT(1);
    ehci.async_qh->next_qtd = QH_LINK_TERMINATE;
    ehci.async_qh->alt_qtd  = QH_LINK_TERMINATE;
    ehci.async_qh->token    = 0; /* Halted, no active transfer */

    /* Tell the controller about our async list */
    ehci_op_write32(EHCI_OP_ASYNCLISTADDR, ehci.async_qh_phys);

    /* Enable async schedule */
    uint32_t cmd = ehci_op_read32(EHCI_OP_USBCMD);
    cmd |= USBCMD_ASYNC_EN;
    ehci_op_write32(EHCI_OP_USBCMD, cmd);

    return KABI_SUCCESS;
}

// ============================================================================
// Control Transfer
// ============================================================================

int ehci_control_transfer(uint8_t dev_addr, uint8_t ep,
                          void *setup, void *data, uint16_t data_len,
                          int direction) {
    if (!ehci.initialized) return KABI_EIO;

    /* Allocate QH and qTDs (2 or 3 depending on data phase) */
    uint32_t qh_phys, setup_phys, data_phys = 0, status_phys;
    ehci_qh_t *qh = (ehci_qh_t *)kmalloc(sizeof(ehci_qh_t), 1, &qh_phys);
    ehci_qtd_t *setup_qtd = (ehci_qtd_t *)kmalloc(sizeof(ehci_qtd_t), 1, &setup_phys);
    ehci_qtd_t *status_qtd = (ehci_qtd_t *)kmalloc(sizeof(ehci_qtd_t), 1, &status_phys);
    ehci_qtd_t *data_qtd = NULL;

    if (!qh || !setup_qtd || !status_qtd) goto fail;

    memory_set((uint8_t *)qh, 0, sizeof(ehci_qh_t));
    memory_set((uint8_t *)setup_qtd, 0, sizeof(ehci_qtd_t));
    memory_set((uint8_t *)status_qtd, 0, sizeof(ehci_qtd_t));

    /* Copy setup packet to a physically-addressed buffer */
    uint32_t setup_buf_phys;
    uint8_t *setup_buf = (uint8_t *)kmalloc(8, 1, &setup_buf_phys);
    if (!setup_buf) goto fail;
    memory_copy((uint8_t *)setup, setup_buf, 8);

    /* Data buffer (if any) */
    uint8_t *data_buf = NULL;
    uint32_t data_buf_phys = 0;
    if (data_len > 0 && data) {
        data_qtd = (ehci_qtd_t *)kmalloc(sizeof(ehci_qtd_t), 1, &data_phys);
        if (!data_qtd) goto fail;
        memory_set((uint8_t *)data_qtd, 0, sizeof(ehci_qtd_t));

        data_buf = (uint8_t *)kmalloc(data_len, 1, &data_buf_phys);
        if (!data_buf) goto fail;
        memory_set(data_buf, 0, data_len);

        if (direction == USB_DIR_OUT) {
            memory_copy((uint8_t *)data, data_buf, data_len);
        }
    }

    /* ---------- Build SETUP qTD ---------- */
    setup_qtd->next_qtd = data_len > 0 ? data_phys : status_phys;
    setup_qtd->alt_qtd  = QH_LINK_TERMINATE;
    setup_qtd->token    = QTD_TOKEN_ACTIVE | QTD_TOKEN_PID_SETUP |
                          QTD_TOKEN_CERR(3) | QTD_TOKEN_TOTAL_BYTES(8);
    setup_qtd->buffer[0] = setup_buf_phys;

    /* ---------- Build DATA qTD (optional) ---------- */
    if (data_qtd) {
        uint32_t pid = (direction == USB_DIR_IN) ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT;
        data_qtd->next_qtd = status_phys;
        data_qtd->alt_qtd  = QH_LINK_TERMINATE;
        data_qtd->token    = QTD_TOKEN_ACTIVE | pid | QTD_TOKEN_TOGGLE |
                             QTD_TOKEN_CERR(3) | QTD_TOKEN_TOTAL_BYTES(data_len);
        data_qtd->buffer[0] = data_buf_phys;
    }

    /* ---------- Build STATUS qTD ---------- */
    uint32_t status_pid = (data_len > 0 && direction == USB_DIR_IN)
                          ? QTD_TOKEN_PID_OUT : QTD_TOKEN_PID_IN;
    status_qtd->next_qtd = QH_LINK_TERMINATE;
    status_qtd->alt_qtd  = QH_LINK_TERMINATE;
    status_qtd->token    = QTD_TOKEN_ACTIVE | status_pid | QTD_TOKEN_TOGGLE |
                           QTD_TOKEN_CERR(3) | QTD_TOKEN_IOC;

    /* ---------- Build QH ---------- */
    qh->ep_chars = QH_EP_ADDR(dev_addr) | QH_EP_NUM(ep) | QH_EP_SPEED_HIGH |
                   QH_EP_DTC | QH_EP_MAX_PACKET(64) | QH_EP_NAK_RELOAD(4);
    if (dev_addr == 0) qh->ep_chars |= QH_EP_CTRL_EP;
    qh->ep_caps  = QH_CAP_MULT(1);
    qh->next_qtd = setup_phys;
    qh->alt_qtd  = QH_LINK_TERMINATE;
    qh->token    = 0;

    /* ---------- Insert QH into async schedule ---------- */
    qh->next_qh = ehci.async_qh->next_qh;
    ehci.async_qh->next_qh = qh_phys | QH_LINK_TYPE_QH;

    /* ---------- Wait for completion ---------- */
    int timeout = 500;
    while (timeout-- > 0) {
        uint32_t tok = status_qtd->token;
        if (!(tok & QTD_TOKEN_ACTIVE)) break;
        ehci_delay(1);
    }

    /* Remove QH from schedule */
    ehci.async_qh->next_qh = qh->next_qh;

    /* Check result */
    uint32_t final_token = status_qtd->token;
    int result = KABI_SUCCESS;

    if (final_token & QTD_TOKEN_ACTIVE) {
        kprint("[EHCI] Control transfer timeout\n");
        result = KABI_EIO;
    } else if (QTD_TOKEN_GET_STATUS(final_token) & 0x7E) {
        /* Any error bits set (HALTED, BABBLE, XACTERR, etc.) */
        result = KABI_EIO;
    } else if (data_len > 0 && direction == USB_DIR_IN && data_buf) {
        memory_copy(data_buf, (uint8_t *)data, data_len);
    }

    /* Free everything */
    if (data_buf) kfree(data_buf);
    if (data_qtd) kfree(data_qtd);
    kfree(setup_buf);
    kfree(status_qtd);
    kfree(setup_qtd);
    kfree(qh);
    return result;

fail:
    if (data_buf) kfree(data_buf);
    if (data_qtd) kfree(data_qtd);
    if (status_qtd) kfree(status_qtd);
    if (setup_qtd) kfree(setup_qtd);
    if (qh) kfree(qh);
    return KABI_ENOMEM;
}

// ============================================================================
// Bulk Transfer
// ============================================================================

int ehci_bulk_transfer(uint8_t dev_addr, uint8_t ep,
                       void *data, uint16_t data_len,
                       int direction, uint8_t *toggle) {
    if (!ehci.initialized || !data || data_len == 0 || !toggle) return KABI_EIO;

    uint32_t qh_phys, qtd_phys;
    ehci_qh_t *qh = (ehci_qh_t *)kmalloc(sizeof(ehci_qh_t), 1, &qh_phys);
    ehci_qtd_t *qtd = (ehci_qtd_t *)kmalloc(sizeof(ehci_qtd_t), 1, &qtd_phys);
    if (!qh || !qtd) {
        if (qh) kfree(qh);
        if (qtd) kfree(qtd);
        return KABI_ENOMEM;
    }
    memory_set((uint8_t *)qh, 0, sizeof(ehci_qh_t));
    memory_set((uint8_t *)qtd, 0, sizeof(ehci_qtd_t));

    /* Data buffer copy */
    uint32_t buf_phys;
    uint8_t *buf = (uint8_t *)kmalloc(data_len, 1, &buf_phys);
    if (!buf) { kfree(qh); kfree(qtd); return KABI_ENOMEM; }

    if (direction == USB_DIR_OUT) {
        memory_copy((uint8_t *)data, buf, data_len);
    } else {
        memory_set(buf, 0, data_len);
    }

    /* Build qTD */
    uint32_t pid = (direction == USB_DIR_IN) ? QTD_TOKEN_PID_IN : QTD_TOKEN_PID_OUT;
    uint32_t toggle_bit = (*toggle) ? QTD_TOKEN_TOGGLE : 0;

    qtd->next_qtd = QH_LINK_TERMINATE;
    qtd->alt_qtd  = QH_LINK_TERMINATE;
    qtd->token    = QTD_TOKEN_ACTIVE | pid | toggle_bit |
                    QTD_TOKEN_CERR(3) | QTD_TOKEN_IOC |
                    QTD_TOKEN_TOTAL_BYTES(data_len);
    qtd->buffer[0] = buf_phys;
    /* Handle page crossings for buffers > 4K */
    for (int i = 1; i < 5 && (data_len > (uint16_t)(i * 4096)); i++) {
        qtd->buffer[i] = (buf_phys & ~0xFFF) + (i * 4096);
    }

    /* Build QH */
    uint8_t ep_num = ep & 0x0F;
    uint16_t max_pkt = 512;
    qh->ep_chars = QH_EP_ADDR(dev_addr) | QH_EP_NUM(ep_num) | QH_EP_SPEED_HIGH |
                   QH_EP_DTC | QH_EP_MAX_PACKET(max_pkt) | QH_EP_NAK_RELOAD(4);
    qh->ep_caps  = QH_CAP_MULT(1);
    qh->next_qtd = qtd_phys;
    qh->alt_qtd  = QH_LINK_TERMINATE;
    qh->token    = 0;

    /* Insert into async schedule */
    qh->next_qh = ehci.async_qh->next_qh;
    ehci.async_qh->next_qh = qh_phys | QH_LINK_TYPE_QH;

    /* Wait for completion */
    int timeout = 2000;
    while (timeout-- > 0) {
        if (!(qtd->token & QTD_TOKEN_ACTIVE)) break;
        ehci_delay(1);
    }

    /* Remove from schedule */
    ehci.async_qh->next_qh = qh->next_qh;

    int result = KABI_SUCCESS;
    if (qtd->token & QTD_TOKEN_ACTIVE) {
        result = KABI_EIO;
    } else if (QTD_TOKEN_GET_STATUS(qtd->token) & 0x7E) {
        result = KABI_EIO;
    } else {
        /* Success: Update toggle based on number of packets */
        int num_packets = (data_len + max_pkt - 1) / max_pkt;
        if (num_packets == 0) num_packets = 1; /* Always at least one packet */
        if (num_packets % 2) {
            *toggle ^= 1;
        }
        
        if (direction == USB_DIR_IN) {
            memory_copy(buf, (uint8_t *)data, data_len);
        }
    }

    kfree(buf);
    kfree(qtd);
    kfree(qh);
    return result;
}

// ============================================================================
// Port Operations
// ============================================================================

int ehci_port_count(void) {
    return ehci.initialized ? ehci.num_ports : 0;
}

uint32_t ehci_port_status(int port) {
    if (!ehci.initialized || port < 0 || port >= ehci.num_ports) return 0;
    return ehci_op_read32(EHCI_OP_PORTSC(port));
}

void ehci_port_reset(int port) {
    if (!ehci.initialized || port < 0 || port >= ehci.num_ports) return;

    uint32_t portsc = ehci_op_read32(EHCI_OP_PORTSC(port));
    portsc |= PORTSC_RESET;
    portsc &= ~PORTSC_ENABLED;
    ehci_op_write32(EHCI_OP_PORTSC(port), portsc);

    ehci_delay(50);

    portsc = ehci_op_read32(EHCI_OP_PORTSC(port));
    portsc &= ~PORTSC_RESET;
    ehci_op_write32(EHCI_OP_PORTSC(port), portsc);

    ehci_delay(10);

    /* Clear change bits */
    portsc = ehci_op_read32(EHCI_OP_PORTSC(port));
    portsc |= PORTSC_CONNECT_CHANGE | PORTSC_ENABLE_CHANGE | PORTSC_OC_CHANGE;
    ehci_op_write32(EHCI_OP_PORTSC(port), portsc);
}

// ============================================================================
// IRQ Handler
// ============================================================================

static void ehci_irq_handler(void *regs) {
    (void)regs;
    uint32_t sts = ehci_op_read32(EHCI_OP_USBSTS);
    uint32_t intr = ehci_op_read32(EHCI_OP_USBINTR);
    sts &= intr;
    if (!sts) return;
    ehci_op_write32(EHCI_OP_USBSTS, sts);

    if (sts & USBSTS_PORT_CHANGE) {
        kprint("[EHCI] Port status change\n");
    }
    if (sts & USBSTS_HOST_SYSTEM_ERR) {
        kprint("[EHCI] Host system error!\n");
    }
}

// ============================================================================
// Init
// ============================================================================

int ehci_init(void) {
    memory_set((uint8_t *)&ehci, 0, sizeof(ehci));

    kprint("[EHCI] Probing for EHCI controller...\n");
    if (!pci_find_device(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_EHCI, &ehci.pci_dev)) {
        kprint("[EHCI] No EHCI controller found\n");
        return KABI_ENOENT;
    }

    char s[16];
    kprint("[EHCI] Found at PCI ");
    kabi_int_to_ascii(ehci.pci_dev.bus, s); kprint(s); kprint(":");
    kabi_int_to_ascii(ehci.pci_dev.slot, s); kprint(s); kprint(".");
    kabi_int_to_ascii(ehci.pci_dev.func, s); kprint(s);
    kprint(" vendor=0x"); kabi_hex_to_ascii(ehci.pci_dev.vendor_id, s); kprint(s);
    kprint(" device=0x"); kabi_hex_to_ascii(ehci.pci_dev.device_id, s); kprint(s);
    kprint("\n");

    pci_enable_bus_master(ehci.pci_dev.bus, ehci.pci_dev.slot, ehci.pci_dev.func);
    pci_enable_mem_space(ehci.pci_dev.bus, ehci.pci_dev.slot, ehci.pci_dev.func);

    ehci.phys_base = pci_get_bar(ehci.pci_dev.bus, ehci.pci_dev.slot,
                                  ehci.pci_dev.func, 0);
    ehci.irq = ehci.pci_dev.irq_line;

    if (ehci.phys_base == 0) {
        kprint("[EHCI] ERROR: BAR0 is NULL\n");
        return KABI_EIO;
    }

    kprint("[EHCI] BAR0=0x"); kabi_hex_to_ascii(ehci.phys_base, s); kprint(s); kprint("\n");

    ehci.cap_base = ehci_map_mmio(ehci.phys_base, 0x1000);

    uint32_t cap0 = ehci_cap_read32(EHCI_CAP_CAPLENGTH);
    ehci.cap_length  = cap0 & 0xFF;
    ehci.hci_version = (cap0 >> 16) & 0xFFFF;
    ehci.hcs_params  = ehci_cap_read32(EHCI_CAP_HCSPARAMS);
    ehci.hcc_params  = ehci_cap_read32(EHCI_CAP_HCCPARAMS);
    ehci.num_ports   = HCSPARAMS_N_PORTS(ehci.hcs_params);

    ehci.op_base = (volatile uint32_t *)((uint8_t *)ehci.cap_base + ehci.cap_length);

    kprint("[EHCI] HCI v0x"); kabi_hex_to_ascii(ehci.hci_version, s); kprint(s);
    kprint(" ports="); kabi_int_to_ascii(ehci.num_ports, s); kprint(s); kprint("\n");

    ehci_bios_handoff();

    int rc = ehci_reset();
    if (rc != KABI_SUCCESS) return rc;

    /* Configure */
    uint32_t cmd = ehci_op_read32(EHCI_OP_USBCMD);
    cmd &= ~(0xFF << 16);
    cmd |= USBCMD_INT_THRESHOLD(8);
    ehci_op_write32(EHCI_OP_USBCMD, cmd);
    ehci_op_write32(EHCI_OP_CTRLDSSEGMENT, 0);
    ehci_op_write32(EHCI_OP_CONFIGFLAG, CONFIGFLAG_CF);

    /* USB 2.0 spec: wait at least 100ms for port routing to settle */
    ehci_delay(100);

    /* Setup async schedule */
    rc = ehci_setup_async();
    if (rc != KABI_SUCCESS) return rc;

    /* Explicitly power on each port */
    for (int i = 0; i < ehci.num_ports; i++) {
        uint32_t portsc = ehci_op_read32(EHCI_OP_PORTSC(i));
        portsc |= PORTSC_POWER;
        ehci_op_write32(EHCI_OP_PORTSC(i), portsc);
    }
    ehci_delay(20); /* Port power stabilization */

    /* Start controller */
    cmd = ehci_op_read32(EHCI_OP_USBCMD);
    cmd |= USBCMD_RUN_STOP;
    ehci_op_write32(EHCI_OP_USBCMD, cmd);

    int timeout = 100;
    while (timeout-- > 0) {
        if (!(ehci_op_read32(EHCI_OP_USBSTS) & USBSTS_HALTED)) break;
        ehci_delay(1);
    }
    if (timeout <= 0) {
        kprint("[EHCI] ERROR: Controller failed to start\n");
        return KABI_EIO;
    }

    /* Enable interrupts */
    ehci_op_write32(EHCI_OP_USBINTR,
        USBINTR_USBINT | USBINTR_USBERRINT |
        USBINTR_PORT_CHANGE | USBINTR_HOST_SYSTEM_ERR);

    if (ehci.irq > 0 && ehci.irq < 16) {
        kabi_irq_register(ehci.irq, ehci_irq_handler);
    }

    ehci.initialized = 1;
    kprint("[EHCI] Controller initialized and running\n");

    /* Wait for USB devices to connect (per USB 2.0 spec: up to 100ms after
     * port power, but QEMU virtual devices may need more time) */
    kprint("[EHCI] Waiting for device connections...\n");
    ehci_delay(200);

    /* Clear any pending port change status from startup */
    uint32_t sts = ehci_op_read32(EHCI_OP_USBSTS);
    ehci_op_write32(EHCI_OP_USBSTS, sts);

    /* Scan ports (with retry — devices may need a moment) */
    int found_any = 0;
    for (int retry = 0; retry < 3 && !found_any; retry++) {
        if (retry > 0) {
            kprint("[EHCI] Retry scan...\n");
            ehci_delay(200);
        }

        for (int i = 0; i < ehci.num_ports; i++) {
            uint32_t portsc = ehci_op_read32(EHCI_OP_PORTSC(i));
            kprint("  Port "); kabi_int_to_ascii(i, s); kprint(s);
            kprint(": 0x"); kabi_hex_to_ascii(portsc, s); kprint(s);
            kprint(portsc & PORTSC_CONNECTED ? " [CONNECTED]" : " [EMPTY]");
            kprint("\n");
            if (portsc & PORTSC_CONNECTED) found_any = 1;
        }
    }

    return KABI_SUCCESS;
}

/* --- Module Registration --- */
static int ehci_module_init(void) { return ehci_init(); }

kabi_module_t __kabi_module_ehci = {
    .name           = "ehci",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = ehci_module_init,
    .exit           = NULL,
    .description    = "EHCI USB 2.0 host controller driver"
};
