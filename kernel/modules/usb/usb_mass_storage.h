#ifndef USB_MASS_STORAGE_H
#define USB_MASS_STORAGE_H

#include "usb_core.h"

/**
 * USB Mass Storage Class Driver (Module)
 *
 * Implements the Bulk-Only Transport (BBB) protocol for USB mass
 * storage devices (flash drives, external disks). Uses SCSI
 * transparent command set (READ_10, WRITE_10, etc.) over USB bulk
 * endpoints, and registers as a block device for FAT32 mounting.
 *
 * Architecture:
 *   [FAT32 VFS]          <-- fat32_vfs_mount(dev_id, "/usb")
 *   [Block Device Layer]  <-- block_dev_register(read/write callbacks)
 *   [Mass Storage]        <-- you are here (SCSI over BBB)
 *   [USB Core]            <-- usb_control_msg, usb_register_class_driver
 *   [EHCI Driver]         <-- ehci_bulk_transfer
 */

/* BBB (Bulk-Only) protocol constants */
#define CBW_SIGNATURE       0x43425355  /* "USBC" */
#define CSW_SIGNATURE       0x53425355  /* "USBS" */
#define CBW_DIRECTION_OUT   0x00
#define CBW_DIRECTION_IN    0x80

/* CSW status codes */
#define CSW_STATUS_PASSED   0x00
#define CSW_STATUS_FAILED   0x01
#define CSW_STATUS_PHASE    0x02

/* SCSI commands */
#define SCSI_TEST_UNIT_READY  0x00
#define SCSI_INQUIRY          0x12
#define SCSI_READ_CAPACITY_10 0x25
#define SCSI_READ_10          0x28
#define SCSI_WRITE_10         0x2A
#define SCSI_REQUEST_SENSE    0x03

/* --- Command Block Wrapper (31 bytes) --- */
typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} __attribute__((packed)) usb_cbw_t;

/* --- Command Status Wrapper (13 bytes) --- */
typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} __attribute__((packed)) usb_csw_t;

/* --- SCSI Inquiry Response (36 bytes) --- */
typedef struct {
    uint8_t  peripheral;
    uint8_t  removable;
    uint8_t  version;
    uint8_t  response_format;
    uint8_t  additional_length;
    uint8_t  reserved[3];
    char     vendor[8];
    char     product[16];
    char     revision[4];
} __attribute__((packed)) scsi_inquiry_t;

/* --- SCSI Read Capacity Response (8 bytes) --- */
typedef struct {
    uint32_t last_lba;      /* Big-endian */
    uint32_t block_size;    /* Big-endian */
} __attribute__((packed)) scsi_read_capacity_t;

/* --- Mass Storage Device State --- */
typedef struct {
    usb_device_t *usb_dev;
    uint8_t  ep_in;         /* Bulk IN endpoint address */
    uint8_t  ep_out;        /* Bulk OUT endpoint address */
    uint16_t ep_in_max;     /* Max packet size IN */
    uint16_t ep_out_max;    /* Max packet size OUT */
    uint32_t tag;           /* CBW tag counter */
    uint32_t num_sectors;   /* Total sectors */
    uint32_t sector_size;   /* Bytes per sector (typically 512) */
    int      block_dev_id;  /* Registered block device index */
    int      ready;         /* Device is ready for I/O */
} msc_device_t;

/* Public API */
int usb_msc_init(void);

#endif
