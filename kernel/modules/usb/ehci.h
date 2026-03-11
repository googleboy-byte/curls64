#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>
#include "../drivers/pci.h"

/**
 * EHCI (Enhanced Host Controller Interface) USB 2.0 Controller Driver
 * Register definitions per EHCI Specification 1.0.
 */

/* PCI class codes for USB controllers */
#define PCI_CLASS_SERIAL_BUS  0x0C
#define PCI_SUBCLASS_USB      0x03
#define PCI_PROGIF_EHCI       0x20

/* --- Capability Registers --- */
#define EHCI_CAP_CAPLENGTH    0x00
#define EHCI_CAP_HCIVERSION   0x02
#define EHCI_CAP_HCSPARAMS    0x04
#define EHCI_CAP_HCCPARAMS    0x08

#define HCSPARAMS_N_PORTS(x)      ((x) & 0xF)
#define HCSPARAMS_N_CC(x)         (((x) >> 12) & 0xF)
#define HCCPARAMS_EECP(x)         (((x) >> 8) & 0xFF)

/* --- Operational Registers --- */
#define EHCI_OP_USBCMD           0x00
#define EHCI_OP_USBSTS           0x04
#define EHCI_OP_USBINTR          0x08
#define EHCI_OP_FRINDEX          0x0C
#define EHCI_OP_CTRLDSSEGMENT    0x10
#define EHCI_OP_PERIODICLISTBASE 0x14
#define EHCI_OP_ASYNCLISTADDR    0x18
#define EHCI_OP_CONFIGFLAG       0x40
#define EHCI_OP_PORTSC(n)        (0x44 + ((n) * 4))

/* USBCMD bits */
#define USBCMD_RUN_STOP          (1 << 0)
#define USBCMD_HCRESET           (1 << 1)
#define USBCMD_PERIODIC_EN       (1 << 4)
#define USBCMD_ASYNC_EN          (1 << 5)
#define USBCMD_INT_THRESHOLD(n)  (((n) & 0xFF) << 16)

/* USBSTS bits */
#define USBSTS_USBINT            (1 << 0)
#define USBSTS_USBERRINT         (1 << 1)
#define USBSTS_PORT_CHANGE       (1 << 2)
#define USBSTS_HOST_SYSTEM_ERR   (1 << 4)
#define USBSTS_HALTED            (1 << 12)

/* USBINTR bits */
#define USBINTR_USBINT           (1 << 0)
#define USBINTR_USBERRINT        (1 << 1)
#define USBINTR_PORT_CHANGE      (1 << 2)
#define USBINTR_HOST_SYSTEM_ERR  (1 << 4)
#define USBINTR_ASYNC_ADVANCE    (1 << 5)

/* PORTSC bits */
#define PORTSC_CONNECTED         (1 << 0)
#define PORTSC_CONNECT_CHANGE    (1 << 1)
#define PORTSC_ENABLED           (1 << 2)
#define PORTSC_ENABLE_CHANGE     (1 << 3)
#define PORTSC_OVERCURRENT       (1 << 4)
#define PORTSC_OC_CHANGE         (1 << 5)
#define PORTSC_RESET             (1 << 8)
#define PORTSC_POWER             (1 << 12)
#define PORTSC_OWNER             (1 << 13)
#define PORTSC_LINE_STATUS(x)    (((x) >> 10) & 3)

#define CONFIGFLAG_CF            (1 << 0)

/* EECP / BIOS handoff */
#define USBLEGSUP_BIOS_OWNED     (1 << 16)
#define USBLEGSUP_OS_OWNED       (1 << 24)

/* --- Queue Head (QH) --- */
typedef struct ehci_qh {
    uint32_t next_qh;
    uint32_t ep_chars;
    uint32_t ep_caps;
    uint32_t current_qtd;
    uint32_t next_qtd;
    uint32_t alt_qtd;
    uint32_t token;
    uint32_t buffer[5];
} __attribute__((aligned(32))) ehci_qh_t;

/* --- Queue Transfer Descriptor (qTD) --- */
typedef struct ehci_qtd {
    uint32_t next_qtd;
    uint32_t alt_qtd;
    uint32_t token;
    uint32_t buffer[5];
} __attribute__((aligned(32))) ehci_qtd_t;

/* QH/qTD link bits */
#define QH_LINK_TERMINATE       (1 << 0)
#define QH_LINK_TYPE_QH         (1 << 1)

/* qTD token bits */
#define QTD_TOKEN_ACTIVE         (1 << 7)
#define QTD_TOKEN_PID_OUT        (0 << 8)
#define QTD_TOKEN_PID_IN         (1 << 8)
#define QTD_TOKEN_PID_SETUP      (2 << 8)
#define QTD_TOKEN_TOGGLE         (1 << 31)
#define QTD_TOKEN_TOTAL_BYTES(n) (((n) & 0x7FFF) << 16)
#define QTD_TOKEN_CERR(n)        (((n) & 3) << 10)
#define QTD_TOKEN_IOC            (1 << 15)

/* qTD token status extraction */
#define QTD_TOKEN_GET_STATUS(t)  ((t) & 0xFF)
#define QTD_TOKEN_GET_BYTES(t)   (((t) >> 16) & 0x7FFF)

/* QH endpoint characteristics */
#define QH_EP_ADDR(a)            ((a) & 0x7F)
#define QH_EP_NUM(n)             (((n) & 0xF) << 8)
#define QH_EP_SPEED_HIGH         (2 << 12)
#define QH_EP_SPEED_FULL         (0 << 12)
#define QH_EP_SPEED_LOW          (1 << 12)
#define QH_EP_DTC                (1 << 14)     /* Data toggle from qTD */
#define QH_EP_HRECLAIM           (1 << 15)     /* Head of reclamation list */
#define QH_EP_MAX_PACKET(n)      (((n) & 0x7FF) << 16)
#define QH_EP_CTRL_EP            (1 << 27)     /* Control endpoint flag */
#define QH_EP_NAK_RELOAD(n)      (((n) & 0xF) << 28)

/* QH endpoint capabilities */
#define QH_CAP_MULT(n)           (((n) & 3) << 30)

/* --- Driver State --- */
typedef struct {
    pci_device_t pci_dev;
    volatile uint32_t *cap_base;
    volatile uint32_t *op_base;
    uint32_t phys_base;
    uint8_t  cap_length;
    uint16_t hci_version;
    uint32_t hcs_params;
    uint32_t hcc_params;
    uint8_t  num_ports;
    uint8_t  irq;
    int      initialized;

    /* Async schedule (for control/bulk transfers) */
    ehci_qh_t  *async_qh;          /* Head QH (physical-aligned) */
    uint32_t    async_qh_phys;     /* Physical address of async_qh */
} ehci_controller_t;

/* --- Public API --- */
int      ehci_init(void);
int      ehci_port_count(void);
uint32_t ehci_port_status(int port);
void     ehci_port_reset(int port);

/* Transfer API (used by USB core) */
int ehci_control_transfer(uint8_t dev_addr, uint8_t ep,
                          void *setup, void *data, uint16_t data_len,
                          int direction);

int ehci_bulk_transfer(uint8_t dev_addr, uint8_t ep,
                       void *data, uint16_t data_len,
                       int direction, uint8_t *toggle);

#endif
