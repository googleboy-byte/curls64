#include "usb_mass_storage.h"
#include "usb_core.h"
#include "ehci.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"
#include "../../core/block_dev.h"

/**
 * USB Mass Storage Class Driver (Module)
 *
 * Implements Bulk-Only Transport (BBB) per USB Mass Storage Class spec.
 * Uses SCSI transparent command set (READ_10, WRITE_10) over bulk
 * endpoints. Registers discovered storage devices with the kernel
 * block device layer so FAT32 can mount them.
 *
 * Architecture:
 *   [FAT32 VFS]          <-- fat32_vfs_mount(dev_id, "/mnt")
 *   [Block Device Layer]  <-- block_dev_register(read/write)
 *   [Mass Storage]        <-- you are here
 *   [USB Core]            <-- enumeration, control transfers
 *   [EHCI Driver]         <-- bulk transfers
 *
 * Helper Libraries:
 *   Kernel-space: libc/ (string.h, mem.h)
 *   See include/module/module_abi_v1.h for full K-ABI reference.
 */

// ============================================================================
// Global State
// ============================================================================

#define MSC_MAX_DEVICES 4
static msc_device_t msc_devices[MSC_MAX_DEVICES];
static int msc_num_devices = 0;

// ============================================================================
// Byte-swap helpers (SCSI uses big-endian)
// ============================================================================

static uint32_t bswap32(uint32_t x) {
    return ((x >> 24) & 0xFF) |
           ((x >> 8)  & 0xFF00) |
           ((x << 8)  & 0xFF0000) |
           ((x << 24) & 0xFF000000);
}

// ============================================================================
// BBB Transport
// ============================================================================

static int msc_send_cbw(msc_device_t *dev, usb_cbw_t *cbw) {
    return usb_bulk_msg(dev->usb_dev, dev->ep_out,
                        cbw, sizeof(usb_cbw_t), USB_DIR_OUT);
}

static int msc_recv_csw(msc_device_t *dev, usb_csw_t *csw) {
    return usb_bulk_msg(dev->usb_dev, dev->ep_in,
                        csw, sizeof(usb_csw_t), USB_DIR_IN);
}

static int msc_bulk_in(msc_device_t *dev, void *data, uint16_t len) {
    return usb_bulk_msg(dev->usb_dev, dev->ep_in,
                        data, len, USB_DIR_IN);
}

static int msc_bulk_out(msc_device_t *dev, void *data, uint16_t len) {
    return usb_bulk_msg(dev->usb_dev, dev->ep_out,
                        data, len, USB_DIR_OUT);
}

/**
 * Execute a SCSI command via BBB transport:
 *   1. Send CBW (Command Block Wrapper)
 *   2. Data phase (optional IN or OUT)
 *   3. Receive CSW (Command Status Wrapper)
 */
static int msc_scsi_command(msc_device_t *dev, uint8_t *cdb, uint8_t cdb_len,
                            void *data, uint32_t data_len, int direction) {
    usb_cbw_t cbw;
    memory_set((uint8_t *)&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = CBW_SIGNATURE;
    cbw.dCBWTag       = ++dev->tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags    = (direction == USB_DIR_IN) ? CBW_DIRECTION_IN : CBW_DIRECTION_OUT;
    cbw.bCBWLUN       = 0;
    cbw.bCBWCBLength  = cdb_len;
    memory_copy(cdb, cbw.CBWCB, cdb_len);

    /* Phase 1: Send CBW */
    int rc = msc_send_cbw(dev, &cbw);
    if (rc != KABI_SUCCESS) {
        kprint("[MSC] CBW send failed\n");
        return rc;
    }

    /* Phase 2: Data transfer (if any) */
    if (data_len > 0 && data) {
        if (direction == USB_DIR_IN) {
            rc = msc_bulk_in(dev, data, (uint16_t)data_len);
        } else {
            rc = msc_bulk_out(dev, data, (uint16_t)data_len);
        }
        if (rc != KABI_SUCCESS) {
            kprint("[MSC] Data phase failed\n");
            return rc;
        }
    }

    /* Phase 3: Receive CSW */
    usb_csw_t csw;
    memory_set((uint8_t *)&csw, 0, sizeof(csw));
    rc = msc_recv_csw(dev, &csw);
    if (rc != KABI_SUCCESS) {
        kprint("[MSC] CSW receive failed\n");
        return rc;
    }

    if (csw.dCSWSignature != CSW_SIGNATURE) {
        kprint("[MSC] Invalid CSW signature\n");
        return KABI_EIO;
    }

    if (csw.bCSWStatus != CSW_STATUS_PASSED) {
        return KABI_EIO;
    }

    return KABI_SUCCESS;
}

// ============================================================================
// SCSI Commands
// ============================================================================

static int msc_test_unit_ready(msc_device_t *dev) {
    uint8_t cdb[6];
    memory_set(cdb, 0, 6);
    cdb[0] = SCSI_TEST_UNIT_READY;
    return msc_scsi_command(dev, cdb, 6, NULL, 0, USB_DIR_IN);
}

static int msc_inquiry(msc_device_t *dev, scsi_inquiry_t *inq) {
    uint8_t cdb[6];
    memory_set(cdb, 0, 6);
    cdb[0] = SCSI_INQUIRY;
    cdb[4] = 36; /* Allocation length */
    return msc_scsi_command(dev, cdb, 6, inq, 36, USB_DIR_IN);
}

static int msc_read_capacity(msc_device_t *dev) {
    uint8_t cdb[10];
    memory_set(cdb, 0, 10);
    cdb[0] = SCSI_READ_CAPACITY_10;

    scsi_read_capacity_t cap;
    int rc = msc_scsi_command(dev, cdb, 10, &cap, 8, USB_DIR_IN);
    if (rc != KABI_SUCCESS) return rc;

    dev->num_sectors  = bswap32(cap.last_lba) + 1;
    dev->sector_size  = bswap32(cap.block_size);

    return KABI_SUCCESS;
}

static int msc_read_sector(msc_device_t *dev, uint32_t lba, uint8_t *buf) {
    uint8_t cdb[10];
    memory_set(cdb, 0, 10);
    cdb[0] = SCSI_READ_10;
    /* LBA in big-endian */
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8)  & 0xFF;
    cdb[5] = lba & 0xFF;
    /* Transfer length = 1 sector */
    cdb[7] = 0;
    cdb[8] = 1;

    return msc_scsi_command(dev, cdb, 10, buf, 512, USB_DIR_IN);
}

static int msc_write_sector(msc_device_t *dev, uint32_t lba, uint8_t *buf) {
    uint8_t cdb[10];
    memory_set(cdb, 0, 10);
    cdb[0] = SCSI_WRITE_10;
    cdb[2] = (lba >> 24) & 0xFF;
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8)  & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[7] = 0;
    cdb[8] = 1;

    return msc_scsi_command(dev, cdb, 10, buf, 512, USB_DIR_OUT);
}

// ============================================================================
// Block Device Callbacks (bridge to KABI block_dev layer)
// ============================================================================

/* We need static wrappers because block_dev expects (lba, buf) signature */
static int msc_block_read_0(uint64_t lba, uint8_t *buf)  { return msc_read_sector(&msc_devices[0], (uint32_t)lba, buf); }
static int msc_block_write_0(uint64_t lba, uint8_t *buf) { return msc_write_sector(&msc_devices[0], (uint32_t)lba, buf); }
static int msc_block_read_1(uint64_t lba, uint8_t *buf)  { return msc_read_sector(&msc_devices[1], (uint32_t)lba, buf); }
static int msc_block_write_1(uint64_t lba, uint8_t *buf) { return msc_write_sector(&msc_devices[1], (uint32_t)lba, buf); }

static block_read_type_t msc_read_funcs[]  = { msc_block_read_0,  msc_block_read_1 };
static block_write_type_t msc_write_funcs[] = { msc_block_write_0, msc_block_write_1 };

// ============================================================================
// Class Driver Probe (called by USB core when device matches)
// ============================================================================

static int msc_probe(usb_device_t *dev) {
    if (msc_num_devices >= MSC_MAX_DEVICES || msc_num_devices >= 2) {
        kprint("[MSC] Max devices reached\n");
        return KABI_ENOMEM;
    }

    msc_device_t *msc = &msc_devices[msc_num_devices];
    memory_set((uint8_t *)msc, 0, sizeof(msc_device_t));
    msc->usb_dev = dev;
    msc->tag = 0;

    /* Find bulk IN and bulk OUT endpoints */
    for (int i = 0; i < dev->num_endpoints; i++) {
        usb_endpoint_t *ep = &dev->endpoints[i];
        if (ep->type == USB_EP_TYPE_BULK) {
            if (ep->address & 0x80) {
                msc->ep_in = ep->address;
                msc->ep_in_max = ep->max_packet;
            } else {
                msc->ep_out = ep->address;
                msc->ep_out_max = ep->max_packet;
            }
        }
    }

    if (!msc->ep_in || !msc->ep_out) {
        kprint("[MSC] Missing bulk endpoints\n");
        return KABI_EIO;
    }

    char s[16];
    kprint("[MSC] Bulk IN=0x");
    kabi_hex_to_ascii(msc->ep_in, s); kprint(s);
    kprint(" OUT=0x");
    kabi_hex_to_ascii(msc->ep_out, s); kprint(s);
    kprint("\n");

    /* Wait for device to be ready */
    int retries = 5;
    while (retries-- > 0) {
        if (msc_test_unit_ready(msc) == KABI_SUCCESS) break;
        /* Small delay between retries */
        volatile uint32_t d = 100000; while (d--) asm volatile("nop");
    }

    /* SCSI INQUIRY */
    scsi_inquiry_t inq;
    memory_set((uint8_t *)&inq, 0, sizeof(inq));
    if (msc_inquiry(msc, &inq) == KABI_SUCCESS) {
        /* Null-terminate vendor/product strings */
        char vendor[9], product[17];
        memory_copy((uint8_t *)inq.vendor, (uint8_t *)vendor, 8);
        vendor[8] = '\0';
        memory_copy((uint8_t *)inq.product, (uint8_t *)product, 16);
        product[16] = '\0';

        kprint("[MSC] Vendor: "); kprint(vendor);
        kprint(" Product: "); kprint(product); kprint("\n");
    }

    /* READ CAPACITY */
    if (msc_read_capacity(msc) == KABI_SUCCESS) {
        kprint("[MSC] Capacity: ");
        kabi_int_to_ascii(msc->num_sectors, s); kprint(s);
        kprint(" sectors x ");
        kabi_int_to_ascii(msc->sector_size, s); kprint(s);
        kprint(" bytes\n");

        /* Calculate size in MB */
        uint32_t mb = (msc->num_sectors / 2048);
        kprint("[MSC] Size: ");
        kabi_int_to_ascii(mb, s); kprint(s);
        kprint(" MB\n");
    }

    /* Register as block device */
    kabi_block_device_t blk;
    memory_set((uint8_t *)&blk, 0, sizeof(blk));
    strcpy(blk.name, "usb0");
    if (msc_num_devices > 0) {
        blk.name[3] = '0' + msc_num_devices;
    }
    blk.read_sector  = msc_read_funcs[msc_num_devices];
    blk.write_sector = msc_write_funcs[msc_num_devices];
    blk.size = msc->num_sectors;

    msc->block_dev_id = block_dev_register(blk);
    msc->ready = 1;

    kprint("[MSC] Registered as block device '");
    kprint(blk.name);
    kprint("' (dev");
    kabi_int_to_ascii(msc->block_dev_id, s); kprint(s);
    kprint(")\n");

    /* Try to auto-mount as FAT32.
     * Strategy: 
     * 1. Try usbNp1 (first partition)
     * 2. Fallback to usbN (whole disk / superfloppy)
     */
    extern int fat32_vfs_mount(uint32_t dev, const char *mountpoint);
    char p1_name[34];
    strcpy(p1_name, blk.name);
    strcat(p1_name, "p1");
    
    kabi_block_device_t *p1_dev = block_dev_get_by_name(p1_name);
    uint32_t mount_dev = msc->block_dev_id;
    if (p1_dev) {
        // Find p1_dev index
        int count = block_dev_get_count();
        for (int i = 0; i < count; i++) {
            if (block_dev_get_by_index(i) == p1_dev) {
                mount_dev = i;
                break;
            }
        }
    }

    if (fat32_vfs_mount(mount_dev, "/mnt") == 0) {
        kprint("[MSC] FAT32 auto-mounted at /mnt from ");
        kprint(p1_dev ? p1_name : blk.name);
        kprint("\n");
    } else {
        kprint("[MSC] FAT32 auto-mount failed. Use 'devs' to see partitions.\n");
    }

    dev->driver_data = msc;
    msc_num_devices++;

    return KABI_SUCCESS;
}

static void msc_disconnect(usb_device_t *dev) {
    msc_device_t *msc = (msc_device_t *)dev->driver_data;
    if (msc) {
        msc->ready = 0;
        kprint("[MSC] Device disconnected\n");
    }
}

// ============================================================================
// Class Driver Registration
// ============================================================================

static usb_class_driver_t msc_class_driver = {
    .name           = "mass_storage",
    .match_class    = USB_CLASS_MASS_STORAGE,   /* 0x08 */
    .match_subclass = USB_MSC_SUBCLASS_SCSI,    /* 0x06 */
    .match_protocol = USB_MSC_PROTOCOL_BBB,     /* 0x50 */
    .probe          = msc_probe,
    .disconnect     = msc_disconnect,
};

int usb_msc_init(void) {
    kprint("[MSC] USB Mass Storage driver initializing...\n");
    msc_num_devices = 0;
    memory_set((uint8_t *)msc_devices, 0, sizeof(msc_devices));
    return usb_register_class_driver(&msc_class_driver);
}

/* --- Module Registration --- */
static int msc_module_init(void) { return usb_msc_init(); }

kabi_module_t __kabi_module_mass_storage = {
    .name           = "mass_storage",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0100,
    .init           = msc_module_init,
    .exit           = NULL,
    .description    = "USB mass storage class driver (SCSI over Bulk-Only Transport)"
};
