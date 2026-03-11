#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>
#include "../drivers/pci.h"

/**
 * UHCI (Universal Host Controller Interface) USB 1.1 Controller Driver
 * Register definitions per UHCI Specification 1.1.
 *
 * UHCI uses I/O port-based access (unlike EHCI which uses MMIO).
 */

/* PCI class codes (shared with EHCI) */
#ifndef PCI_CLASS_SERIAL_BUS
#define PCI_CLASS_SERIAL_BUS  0x0C
#define PCI_SUBCLASS_USB      0x03
#endif
#define PCI_PROGIF_UHCI       0x00

/* --- I/O Registers (offsets from I/O base) --- */
#define UHCI_USBCMD           0x00    /* USB Command (16-bit) */
#define UHCI_USBSTS           0x02    /* USB Status (16-bit) */
#define UHCI_USBINTR          0x04    /* USB Interrupt Enable (16-bit) */
#define UHCI_FRNUM            0x06    /* Frame Number (16-bit) */
#define UHCI_FLBASEADD        0x08    /* Frame List Base Address (32-bit) */
#define UHCI_SOFMOD            0x0C    /* Start of Frame Modify (8-bit) */
#define UHCI_PORTSC1          0x10    /* Port 1 Status/Control (16-bit) */
#define UHCI_PORTSC2          0x12    /* Port 2 Status/Control (16-bit) */

/* USBCMD bits */
#define UHCI_CMD_RUN           (1 << 0)
#define UHCI_CMD_HCRESET       (1 << 1)
#define UHCI_CMD_GRESET        (1 << 2)
#define UHCI_CMD_SWDBG         (1 << 5)
#define UHCI_CMD_MAXP          (1 << 7)   /* Max Packet = 64 bytes */

/* USBSTS bits */
#define UHCI_STS_USBINT        (1 << 0)
#define UHCI_STS_ERROR         (1 << 1)
#define UHCI_STS_RESUME        (1 << 2)
#define UHCI_STS_HSE           (1 << 3)   /* Host System Error */
#define UHCI_STS_HCPE          (1 << 4)   /* HC Process Error */
#define UHCI_STS_HALTED        (1 << 5)

/* PORTSC bits */
#define UHCI_PORT_CONNECTED    (1 << 0)
#define UHCI_PORT_CONNECT_CHG  (1 << 1)
#define UHCI_PORT_ENABLED      (1 << 2)
#define UHCI_PORT_ENABLE_CHG   (1 << 3)
#define UHCI_PORT_LINE_D_PLUS  (1 << 4)
#define UHCI_PORT_LINE_D_MINUS (1 << 5)
#define UHCI_PORT_RESUME       (1 << 6)
#define UHCI_PORT_LOW_SPEED    (1 << 8)
#define UHCI_PORT_RESET        (1 << 9)
#define UHCI_PORT_SUSPEND      (1 << 12)

/* --- Transfer Descriptor (TD) — 32 bytes --- */
typedef struct uhci_td {
    uint32_t link;          /* Pointer to next TD/QH */
    uint32_t status;        /* Control and status */
    uint32_t token;         /* Token (PID, address, endpoint, data length) */
    uint32_t buffer;        /* Buffer pointer */
    /* Software fields (not used by hardware, for driver bookkeeping) */
    uint32_t _reserved[4];
} __attribute__((aligned(16))) uhci_td_t;

/* TD link pointer bits */
#define UHCI_TD_LINK_TERMINATE  (1 << 0)
#define UHCI_TD_LINK_QH         (1 << 1)
#define UHCI_TD_LINK_DEPTH      (1 << 2)

/* TD status bits */
#define UHCI_TD_STATUS_ACTIVE    (1 << 23)
#define UHCI_TD_STATUS_STALLED   (1 << 22)
#define UHCI_TD_STATUS_DBUFFER   (1 << 21)
#define UHCI_TD_STATUS_BABBLE    (1 << 20)
#define UHCI_TD_STATUS_NAK       (1 << 19)
#define UHCI_TD_STATUS_CRC       (1 << 18)
#define UHCI_TD_STATUS_BITSTUFF  (1 << 17)
#define UHCI_TD_STATUS_ERROR_MASK 0x7E0000
#define UHCI_TD_STATUS_IOC       (1 << 24)
#define UHCI_TD_STATUS_IOS       (1 << 25)  /* Isochronous */
#define UHCI_TD_STATUS_LS        (1 << 26)  /* Low Speed */
#define UHCI_TD_STATUS_CERR(n)   (((n) & 3) << 27)
#define UHCI_TD_STATUS_SPD       (1 << 29)  /* Short Packet Detect */

/* TD token bits */
#define UHCI_TD_PID_SETUP        0x2D
#define UHCI_TD_PID_IN           0x69
#define UHCI_TD_PID_OUT          0xE1
#define UHCI_TD_TOKEN(pid, addr, ep, toggle, maxlen) \
    ((pid) | ((addr) << 8) | ((ep) << 15) | ((toggle) << 19) | (((maxlen) - 1) << 21))
#define UHCI_TD_TOKEN_GET_LEN(t) ((((t) >> 21) & 0x7FF) + 1)

/* --- Queue Head (QH) — 16 bytes --- */
typedef struct uhci_qh {
    uint32_t head_link;     /* Horizontal link to next QH */
    uint32_t element_link;  /* Vertical link to first TD */
    uint32_t _pad[2];       /* Padding to 16 bytes */
} __attribute__((aligned(16))) uhci_qh_t;

/* --- Driver State --- */
typedef struct {
    pci_device_t pci_dev;
    uint16_t io_base;       /* I/O port base address */
    uint8_t  irq;
    int      num_ports;     /* Always 2 for UHCI */
    int      initialized;

    /* Frame list (1024 entries, 4KB aligned) */
    uint32_t *frame_list;
    uint32_t  frame_list_phys;

    /* Skeleton QH for async transfers */
    uhci_qh_t *async_qh;
    uint32_t   async_qh_phys;
} uhci_controller_t;

/* --- Public API --- */
int      uhci_init(void);
int      uhci_port_count(void);
uint32_t uhci_port_status(int port);
void     uhci_port_reset(int port);

/* Transfer API (same signature as EHCI) */
int uhci_control_transfer(uint8_t dev_addr, uint8_t ep,
                          void *setup, void *data, uint16_t data_len,
                          int direction);

int uhci_bulk_transfer(uint8_t dev_addr, uint8_t ep,
                       void *data, uint16_t data_len,
                       int direction, uint8_t *toggle);

#endif
