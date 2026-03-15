#include "uhci.h"
#include "usb_core.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "../../cpu/ports.h"

/**
 * UHCI USB 1.1 Host Controller Driver
 *
 * Handles full-speed and low-speed USB devices via I/O port-based
 * register access. Uses frame list + TD chains for transfers.
 *
 * Key: all DMA structures (TDs, QHs, frame list, data buffers) MUST use
 * physical addresses obtained via kmalloc(size, align, &phys).
 */

// ============================================================================
// Global State
// ============================================================================

static uhci_controller_t uhci;

// ============================================================================
// I/O Port Helpers
// ============================================================================

static uint16_t uhci_inw(uint16_t reg) {
    return port_word_in(uhci.io_base + reg);
}

static void uhci_outw(uint16_t reg, uint16_t val) {
    port_word_out(uhci.io_base + reg, val);
}

static uint32_t uhci_inl(uint16_t reg) {
    return port_long_in(uhci.io_base + reg);
}

static void uhci_outl(uint16_t reg, uint32_t val) {
    port_long_out(uhci.io_base + reg, val);
}

// ============================================================================
// Delay
// ============================================================================

static void uhci_delay(uint32_t ms) {
    volatile uint32_t count = ms * 10000;
    while (count--) asm volatile("nop");
}

// ============================================================================
// Port Operations
// ============================================================================

int uhci_port_count(void) {
    return uhci.initialized ? uhci.num_ports : 0;
}

uint32_t uhci_port_status(int port) {
    if (!uhci.initialized || port < 0 || port >= uhci.num_ports) return 0;
    return uhci_inw(UHCI_PORTSC1 + port * 2);
}

void uhci_port_reset(int port) {
    if (!uhci.initialized || port < 0 || port >= uhci.num_ports) return;

    uint16_t reg = UHCI_PORTSC1 + port * 2;

    /* Assert port reset */
    uhci_outw(reg, UHCI_PORT_RESET);
    uhci_delay(50);  /* Spec says >= 10ms */

    /* De-assert reset */
    uhci_outw(reg, 0);
    uhci_delay(10);

    /* Enable port — try multiple times */
    for (int i = 0; i < 10; i++) {
        uint16_t status = uhci_inw(reg);

        /* Clear change bits by writing 1 to them */
        if (status & (UHCI_PORT_CONNECT_CHG | UHCI_PORT_ENABLE_CHG)) {
            uhci_outw(reg, (status & ~(UHCI_PORT_RESET | UHCI_PORT_SUSPEND)) |
                           UHCI_PORT_CONNECT_CHG | UHCI_PORT_ENABLE_CHG);
            uhci_delay(5);
            status = uhci_inw(reg);
        }

        if (!(status & UHCI_PORT_CONNECTED)) break;

        /* Try to enable */
        if (!(status & UHCI_PORT_ENABLED)) {
            uhci_outw(reg, UHCI_PORT_ENABLED);
            uhci_delay(10);
            status = uhci_inw(reg);
        }

        if (status & UHCI_PORT_ENABLED) {
            char s[16];
            kprint("[UHCI] Port "); kabi_int_to_ascii(port, s); kprint(s);
            kprint(" enabled (status=0x"); kabi_hex_to_ascii(status, s); kprint(s);
            kprint(")\n");
            return;
        }
        uhci_delay(10);
    }
}

// ============================================================================
// Control Transfer
// ============================================================================

int uhci_control_transfer(uint8_t dev_addr, uint8_t ep,
                          void *setup, void *data, uint16_t data_len,
                          int direction) {
    if (!uhci.initialized) return KABI_EIO;

    char s[16];

    /* Clear any pending status */
    uhci_outw(UHCI_USBSTS, uhci_inw(UHCI_USBSTS));

    int max_pkt = 8; /* EP0 max packet for full-speed */
    int num_data_tds = 0;
    if (data_len > 0) {
        num_data_tds = (data_len + max_pkt - 1) / max_pkt;
    }
    int total_tds = 1 + num_data_tds + 1; /* SETUP + DATA + STATUS */

    /* Allocate TDs with physical addresses */
    uhci_td_t *tds_virt[16];
    phys_addr_t   tds_phys[16];
    if (total_tds > 16) return KABI_ENOMEM;

    for (int i = 0; i < total_tds; i++) {
        tds_virt[i] = (uhci_td_t *)kmalloc(sizeof(uhci_td_t), 1, &tds_phys[i]);
        if (!tds_virt[i]) {
            for (int j = 0; j < i; j++) kfree(tds_virt[j]);
            return KABI_ENOMEM;
        }
        memory_set((uint8_t *)tds_virt[i], 0, sizeof(uhci_td_t));
    }

    /* Allocate setup buffer (8 bytes, physically addressed) */
    phys_addr_t setup_buf_phys;
    uint8_t *setup_buf = (uint8_t *)kmalloc(8, 1, &setup_buf_phys);
    if (!setup_buf) goto fail;
    memory_copy((uint8_t *)setup, setup_buf, 8);

    /* Allocate data buffer if needed */
    uint8_t *data_buf = NULL;
    phys_addr_t data_buf_phys = 0;
    if (data_len > 0 && data) {
        data_buf = (uint8_t *)kmalloc(data_len, 1, &data_buf_phys);
        if (!data_buf) { kfree(setup_buf); goto fail; }
        memory_set(data_buf, 0, data_len);
        if (direction == USB_DIR_OUT) {
            memory_copy((uint8_t *)data, data_buf, data_len);
        }
    }

    /* Link TDs together using physical addresses (depth-first) */
    for (int i = 0; i < total_tds - 1; i++) {
        tds_virt[i]->link = tds_phys[i + 1] | UHCI_TD_LINK_DEPTH;
    }
    tds_virt[total_tds - 1]->link = UHCI_TD_LINK_TERMINATE;

    /* TD 0: SETUP — PID=SETUP, toggle=0, 8 bytes */
    tds_virt[0]->status = UHCI_TD_STATUS_ACTIVE | UHCI_TD_STATUS_CERR(3);
    tds_virt[0]->token = UHCI_TD_PID_SETUP |
                          ((uint32_t)dev_addr << 8) |
                          ((uint32_t)(ep & 0xF) << 15) |
                          (0 << 19) |        /* toggle 0 */
                          ((8 - 1) << 21);   /* maxlen = 8 bytes */
    tds_virt[0]->buffer = setup_buf_phys;

    /* Data TDs */
    uint8_t toggle = 1;
    uint16_t remaining = data_len;
    uint32_t buf_offset = 0;

    for (int i = 0; i < num_data_tds; i++) {
        uint16_t chunk = (remaining > (uint16_t)max_pkt) ? (uint16_t)max_pkt : remaining;
        uint8_t pid = (direction == USB_DIR_IN) ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT;

        tds_virt[1 + i]->status = UHCI_TD_STATUS_ACTIVE | UHCI_TD_STATUS_CERR(3);
        /* Do NOT set SPD — let short packets complete normally via the chain */
        tds_virt[1 + i]->token = pid |
                                  ((uint32_t)dev_addr << 8) |
                                  ((uint32_t)(ep & 0xF) << 15) |
                                  ((uint32_t)toggle << 19) |
                                  ((uint32_t)(chunk - 1) << 21);
        tds_virt[1 + i]->buffer = data_buf_phys + buf_offset;

        buf_offset += chunk;
        remaining -= chunk;
        toggle ^= 1;
    }

    /* STATUS TD — opposite direction of data, or IN for no-data
     * USB spec: IN data→STATUS OUT, OUT data→STATUS IN, no data→STATUS IN */
    uint8_t status_pid = (data_len > 0 && direction == USB_DIR_IN)
                          ? UHCI_TD_PID_OUT : UHCI_TD_PID_IN;
    tds_virt[total_tds - 1]->status = UHCI_TD_STATUS_ACTIVE | UHCI_TD_STATUS_CERR(3) |
                                       UHCI_TD_STATUS_IOC;
    tds_virt[total_tds - 1]->token = status_pid |
                                      ((uint32_t)dev_addr << 8) |
                                      ((uint32_t)(ep & 0xF) << 15) |
                                      (1 << 19) |         /* toggle 1 */
                                      (0x7FF << 21);      /* maxlen = 0 (null packet) */
    tds_virt[total_tds - 1]->buffer = 0;

    /* Ensure QH is clean before inserting */
    uhci.async_qh->element_link = UHCI_TD_LINK_TERMINATE;

    /* Memory barrier */
    asm volatile("" ::: "memory");

    /* Insert first TD into QH */
    uhci.async_qh->element_link = tds_phys[0];

    /* Wait for completion — poll all TDs */
    int timeout = 5000;
    while (timeout-- > 0) {
        /* Check if STATUS TD completed */
        uint32_t st = tds_virt[total_tds - 1]->status;
        if (!(st & UHCI_TD_STATUS_ACTIVE)) break;

        /* Check for stall/error on any TD */
        int error_found = 0;
        for (int i = 0; i < total_tds; i++) {
            uint32_t tst = tds_virt[i]->status;
            if (!(tst & UHCI_TD_STATUS_ACTIVE) && (tst & UHCI_TD_STATUS_STALLED)) {
                error_found = 1;
                break;
            }
        }
        if (error_found) break;

        /* Check UHCI controller status for errors */
        uint16_t usbsts = uhci_inw(UHCI_USBSTS);
        if (usbsts & (UHCI_STS_HSE | UHCI_STS_HCPE)) {
            kprint("[UHCI] Controller error during transfer: 0x");
            kabi_hex_to_ascii(usbsts, s); kprint(s); kprint("\n");
            uhci_outw(UHCI_USBSTS, usbsts);
            break;
        }

        volatile uint32_t d = 200; while (d--) asm volatile("nop");
    }

    /* Dequeue */
    uhci.async_qh->element_link = UHCI_TD_LINK_TERMINATE;

    /* Check result */
    int result = KABI_SUCCESS;
    uint32_t final_status = tds_virt[total_tds - 1]->status;

    if (final_status & UHCI_TD_STATUS_ACTIVE) {
        kprint("[UHCI] Control xfer timeout addr=");
        kabi_int_to_ascii(dev_addr, s); kprint(s);
        kprint(" len="); kabi_int_to_ascii(data_len, s); kprint(s);
        kprint("\n");
        /* Dump all TD statuses for debugging */
        for (int i = 0; i < total_tds; i++) {
            kprint("  TD"); kabi_int_to_ascii(i, s); kprint(s);
            kprint(": status=0x"); kabi_hex_to_ascii(tds_virt[i]->status, s); kprint(s);
            kprint(" token=0x"); kabi_hex_to_ascii(tds_virt[i]->token, s); kprint(s);
            kprint(tds_virt[i]->status & UHCI_TD_STATUS_ACTIVE ? " [ACTIVE]" : " [done]");
            kprint("\n");
        }
        kprint("  USBSTS=0x"); kabi_hex_to_ascii(uhci_inw(UHCI_USBSTS), s); kprint(s);
        kprint(" USBCMD=0x"); kabi_hex_to_ascii(uhci_inw(UHCI_USBCMD), s); kprint(s);
        kprint(" FRNUM="); kabi_int_to_ascii(uhci_inw(UHCI_FRNUM), s); kprint(s);
        kprint("\n");
        result = KABI_EIO;
    } else if (final_status & UHCI_TD_STATUS_ERROR_MASK) {
        kprint("[UHCI] Control transfer error status=0x");
        kabi_hex_to_ascii(final_status, s); kprint(s); kprint("\n");
        result = KABI_EIO;
    } else if (data_len > 0 && direction == USB_DIR_IN && data_buf) {
        /* Copy received data back */
        memory_copy(data_buf, (uint8_t *)data, data_len);
    }

    /* Clear any status bits */
    uhci_outw(UHCI_USBSTS, uhci_inw(UHCI_USBSTS));

    /* Free everything */
    if (data_buf) kfree(data_buf);
    kfree(setup_buf);
    for (int i = 0; i < total_tds; i++) kfree(tds_virt[i]);
    return result;

fail:
    for (int i = 0; i < total_tds; i++) {
        if (tds_virt[i]) kfree(tds_virt[i]);
    }
    return KABI_ENOMEM;
}

// ============================================================================
// Bulk Transfer
// ============================================================================

int uhci_bulk_transfer(uint8_t dev_addr, uint8_t ep,
                       void *data, uint16_t data_len,
                       int direction, uint8_t *toggle) {
    if (!uhci.initialized || !data || data_len == 0 || !toggle) return KABI_EIO;

    /* Ensure controller is running */
    uint16_t cmd = uhci_inw(UHCI_USBCMD);
    uint16_t sts = uhci_inw(UHCI_USBSTS);
    if (!(cmd & UHCI_CMD_RUN) || (sts & UHCI_STS_HALTED)) {
        /* Restart the controller */
        uhci_outw(UHCI_USBSTS, 0xFFFF);  /* Clear all status */
        uhci_outw(UHCI_USBCMD, UHCI_CMD_RUN | UHCI_CMD_MAXP);
        uhci_delay(2);
    }

    /* Clear pending status */
    uhci_outw(UHCI_USBSTS, uhci_inw(UHCI_USBSTS));

    int max_pkt = 64; /* Full-speed bulk max packet */
    int num_tds = (data_len + max_pkt - 1) / max_pkt;
    if (num_tds > 16) return KABI_ENOMEM;

    uhci_td_t *tds_virt[16];
    phys_addr_t   tds_phys[16];

    for (int i = 0; i < num_tds; i++) {
        tds_virt[i] = (uhci_td_t *)kmalloc(sizeof(uhci_td_t), 1, &tds_phys[i]);
        if (!tds_virt[i]) {
            for (int j = 0; j < i; j++) kfree(tds_virt[j]);
            return KABI_ENOMEM;
        }
        memory_set((uint8_t *)tds_virt[i], 0, sizeof(uhci_td_t));
    }

    /* Allocate physically-addressed data buffer */
    phys_addr_t buf_phys;
    uint8_t *buf = (uint8_t *)kmalloc(data_len, 1, &buf_phys);
    if (!buf) {
        for (int i = 0; i < num_tds; i++) kfree(tds_virt[i]);
        return KABI_ENOMEM;
    }
    memory_set(buf, 0, data_len);
    if (direction == USB_DIR_OUT) {
        memory_copy((uint8_t *)data, buf, data_len);
    }

    /* Link TDs (depth-first) */
    for (int i = 0; i < num_tds - 1; i++) {
        tds_virt[i]->link = tds_phys[i + 1] | UHCI_TD_LINK_DEPTH;
    }
    tds_virt[num_tds - 1]->link = UHCI_TD_LINK_TERMINATE;

    /* Setup each TD */
    uint8_t pid = (direction == USB_DIR_IN) ? UHCI_TD_PID_IN : UHCI_TD_PID_OUT;
    uint8_t endpoint = ep & 0x0F;
    uint8_t current_toggle = *toggle;
    uint16_t remaining = data_len;
    uint32_t buf_offset = 0;

    for (int i = 0; i < num_tds; i++) {
        uint16_t chunk = (remaining > (uint16_t)max_pkt) ? (uint16_t)max_pkt : remaining;

        tds_virt[i]->status = UHCI_TD_STATUS_ACTIVE | UHCI_TD_STATUS_CERR(3);
        if (i == num_tds - 1) tds_virt[i]->status |= UHCI_TD_STATUS_IOC;

        tds_virt[i]->token = pid |
                              ((uint32_t)dev_addr << 8) |
                              ((uint32_t)endpoint << 15) |
                              ((uint32_t)current_toggle << 19) |
                              ((uint32_t)(chunk - 1) << 21);
        tds_virt[i]->buffer = buf_phys + buf_offset;

        buf_offset += chunk;
        remaining -= chunk;
        current_toggle ^= 1;
    }

    /* All TDs are ready. Write them into the QH atomically.
     * The QH element_link currently points to TERMINATE from the previous
     * transfer cleanup. We write the new TD chain pointer directly. */
    asm volatile("" ::: "memory");
    uhci.async_qh->element_link = tds_phys[0];
    asm volatile("" ::: "memory");

    /* Wait for at least one frame to start (FRNUM should advance) */
    uint16_t start_frame = uhci_inw(UHCI_FRNUM);

    /* Wait for completion */
    int timeout = 100000;
    while (timeout-- > 0) {
        uint32_t st = tds_virt[num_tds - 1]->status;
        if (!(st & UHCI_TD_STATUS_ACTIVE)) break;

        /* Also check first TD — if it stalled, no point waiting */
        if (!(tds_virt[0]->status & UHCI_TD_STATUS_ACTIVE) &&
            (tds_virt[0]->status & UHCI_TD_STATUS_STALLED)) {
            break;
        }

        int error_found = 0;
        for (int i = 0; i < num_tds; i++) {
            if (!(tds_virt[i]->status & UHCI_TD_STATUS_ACTIVE) &&
                (tds_virt[i]->status & UHCI_TD_STATUS_ERROR_MASK)) {
                error_found = 1;
                break;
            }
        }
        if (error_found) break;

        volatile uint32_t d = 100; while (d--) asm volatile("nop");
    }

    /* Dequeue — set QH element to TERMINATE before freeing TDs */
    uhci.async_qh->element_link = UHCI_TD_LINK_TERMINATE;

    int result = KABI_SUCCESS;
    uint32_t final_status = tds_virt[num_tds - 1]->status;

    if (final_status & UHCI_TD_STATUS_ACTIVE) {
        char ss[16];
        uint16_t end_frame = uhci_inw(UHCI_FRNUM);
        kprint("[UHCI] Bulk xfer timeout addr=");
        kabi_int_to_ascii(dev_addr, ss); kprint(ss);
        kprint(" ep=0x"); kabi_hex_to_ascii(ep, ss); kprint(ss);
        kprint(" dir="); kprint(direction == USB_DIR_IN ? "IN" : "OUT");
        kprint(" len="); kabi_int_to_ascii(data_len, ss); kprint(ss);
        kprint("\n");
        kprint("  USBCMD=0x"); kabi_hex_to_ascii(uhci_inw(UHCI_USBCMD), ss); kprint(ss);
        kprint(" USBSTS=0x"); kabi_hex_to_ascii(uhci_inw(UHCI_USBSTS), ss); kprint(ss);
        kprint(" FRNUM="); kabi_int_to_ascii(end_frame, ss); kprint(ss);
        kprint(" (start="); kabi_int_to_ascii(start_frame, ss); kprint(ss);
        kprint(")\n");
        kprint("  QH elem=0x"); kabi_hex_to_ascii(uhci.async_qh->element_link, ss); kprint(ss);
        kprint(" TD0 phys=0x"); kabi_hex_to_ascii(tds_phys[0], ss); kprint(ss);
        kprint("\n");
        for (int i = 0; i < num_tds; i++) {
            kprint("  TD"); kabi_int_to_ascii(i, ss); kprint(ss);
            kprint(": stat=0x"); kabi_hex_to_ascii(tds_virt[i]->status, ss); kprint(ss);
            kprint(" tok=0x"); kabi_hex_to_ascii(tds_virt[i]->token, ss); kprint(ss);
            kprint(" buf=0x"); kabi_hex_to_ascii(tds_virt[i]->buffer, ss); kprint(ss);
            kprint("\n");
        }
        result = KABI_EIO;
    } else if (final_status & UHCI_TD_STATUS_ERROR_MASK) {
        char ss[16];
        kprint("[UHCI] Bulk xfer error stat=0x");
        kabi_hex_to_ascii(final_status, ss); kprint(ss); kprint("\n");
        result = KABI_EIO;
    } else {
        /* Success: Update persistent toggle state */
        *toggle = current_toggle;
        if (direction == USB_DIR_IN) {
            memory_copy(buf, (uint8_t *)data, data_len);
        }
    }

    uhci_outw(UHCI_USBSTS, uhci_inw(UHCI_USBSTS));

    kfree(buf);
    for (int i = 0; i < num_tds; i++) kfree(tds_virt[i]);
    return result;
}

// ============================================================================
// Reset
// ============================================================================

static int uhci_reset(void) {
    /* Global reset */
    uhci_outw(UHCI_USBCMD, UHCI_CMD_GRESET);
    uhci_delay(50);
    uhci_outw(UHCI_USBCMD, 0);
    uhci_delay(10);

    /* Host controller reset */
    uhci_outw(UHCI_USBCMD, UHCI_CMD_HCRESET);
    int timeout = 100;
    while (timeout-- > 0) {
        if (!(uhci_inw(UHCI_USBCMD) & UHCI_CMD_HCRESET)) break;
        uhci_delay(1);
    }
    if (timeout <= 0) {
        kprint("[UHCI] Controller reset timeout\n");
        return KABI_EIO;
    }

    if (!(uhci_inw(UHCI_USBSTS) & UHCI_STS_HALTED)) {
        kprint("[UHCI] Controller not halted after reset\n");
    }

    return KABI_SUCCESS;
}

// ============================================================================
// Init
// ============================================================================

int uhci_init(void) {
    memory_set((uint8_t *)&uhci, 0, sizeof(uhci));

    kprint("[UHCI] Probing for UHCI controller...\n");
    if (!pci_find_device(PCI_CLASS_SERIAL_BUS, PCI_SUBCLASS_USB, PCI_PROGIF_UHCI, &uhci.pci_dev)) {
        kprint("[UHCI] No UHCI controller found\n");
        return KABI_ENOENT;
    }

    char s[16];
    kprint("[UHCI] Found at PCI ");
    kabi_int_to_ascii(uhci.pci_dev.bus, s); kprint(s); kprint(":");
    kabi_int_to_ascii(uhci.pci_dev.slot, s); kprint(s); kprint(".");
    kabi_int_to_ascii(uhci.pci_dev.func, s); kprint(s); kprint("\n");

    pci_enable_bus_master(uhci.pci_dev.bus, uhci.pci_dev.slot, uhci.pci_dev.func);

    /* UHCI uses I/O BAR (BAR4 = offset 0x20) */
    uhci.io_base = (uint16_t)(pci_get_bar(uhci.pci_dev.bus, uhci.pci_dev.slot,
                                           uhci.pci_dev.func, 4) & ~0x3);
    uhci.irq = uhci.pci_dev.irq_line;

    if (uhci.io_base == 0) {
        kprint("[UHCI] ERROR: I/O base is NULL\n");
        return KABI_EIO;
    }

    kprint("[UHCI] I/O base=0x");
    kabi_hex_to_ascii(uhci.io_base, s); kprint(s); kprint("\n");

    /* Reset */
    int rc = uhci_reset();
    if (rc != KABI_SUCCESS) return rc;

    kprint("[UHCI] Controller reset complete\n");

    /* UHCI always has 2 root hub ports */
    uhci.num_ports = 2;

    /* Allocate frame list (1024 entries, 4KB aligned) — need physical address */
    phys_addr_t tmp_frame_list_phys;
    uhci.frame_list = (uint32_t *)kmalloc(4096, 1, &tmp_frame_list_phys);
    uhci.frame_list_phys = (uint32_t)tmp_frame_list_phys;
    if (!uhci.frame_list) {
        kprint("[UHCI] Failed to allocate frame list\n");
        return KABI_ENOMEM;
    }

    /* Allocate async QH — need physical address */
    phys_addr_t tmp_async_qh_phys;
    uhci.async_qh = (uhci_qh_t *)kmalloc(sizeof(uhci_qh_t), 1, &tmp_async_qh_phys);
    uhci.async_qh_phys = (uint32_t)tmp_async_qh_phys;
    if (!uhci.async_qh) {
        kfree(uhci.frame_list);
        kprint("[UHCI] Failed to allocate async QH\n");
        return KABI_ENOMEM;
    }
    memory_set((uint8_t *)uhci.async_qh, 0, sizeof(uhci_qh_t));
    uhci.async_qh->head_link = UHCI_TD_LINK_TERMINATE;
    uhci.async_qh->element_link = UHCI_TD_LINK_TERMINATE;

    /* Initialize all frame list entries to point to the async QH */
    for (int i = 0; i < 1024; i++) {
        uhci.frame_list[i] = uhci.async_qh_phys | UHCI_TD_LINK_QH;
    }

    kprint("[UHCI] Frame list phys=0x");
    kabi_hex_to_ascii(uhci.frame_list_phys, s); kprint(s);
    kprint(" QH phys=0x");
    kabi_hex_to_ascii(uhci.async_qh_phys, s); kprint(s);
    kprint("\n");

    /* Set frame list base address */
    uhci_outl(UHCI_FLBASEADD, uhci.frame_list_phys);
    uhci_outw(UHCI_FRNUM, 0);

    /* Disable interrupts (we poll) */
    uhci_outw(UHCI_USBINTR, 0);

    /* Clear any pending status */
    uhci_outw(UHCI_USBSTS, 0xFFFF);

    /* Start controller: run + max packet 64 */
    uhci_outw(UHCI_USBCMD, UHCI_CMD_RUN | UHCI_CMD_MAXP);

    uhci_delay(100);

    /* Verify running */
    uint16_t sts = uhci_inw(UHCI_USBSTS);
    if (sts & UHCI_STS_HALTED) {
        kprint("[UHCI] WARNING: Controller still halted after start\n");
    } else {
        kprint("[UHCI] Controller running (cmd=0x");
        kabi_hex_to_ascii(uhci_inw(UHCI_USBCMD), s); kprint(s);
        kprint(" sts=0x");
        kabi_hex_to_ascii(sts, s); kprint(s);
        kprint(")\n");
    }

    uhci.initialized = 1;

    /* Wait for device connections */
    kprint("[UHCI] Scanning ports...\n");
    uhci_delay(200);

    /* Scan ports */
    int found_any = 0;
    for (int retry = 0; retry < 3 && !found_any; retry++) {
        if (retry > 0) {
            kprint("[UHCI] Retry scan...\n");
            uhci_delay(200);
        }
        for (int i = 0; i < uhci.num_ports; i++) {
            uint16_t portsc = uhci_inw(UHCI_PORTSC1 + i * 2);
            kprint("  Port "); kabi_int_to_ascii(i, s); kprint(s);
            kprint(": 0x"); kabi_hex_to_ascii(portsc, s); kprint(s);
            kprint(portsc & UHCI_PORT_CONNECTED ? " [CONNECTED]" : " [EMPTY]");
            if (portsc & UHCI_PORT_LOW_SPEED) kprint(" [LOW-SPEED]");
            else if (portsc & UHCI_PORT_CONNECTED) kprint(" [FULL-SPEED]");
            kprint("\n");
            if (portsc & UHCI_PORT_CONNECTED) found_any = 1;
        }
    }

    return KABI_SUCCESS;
}
