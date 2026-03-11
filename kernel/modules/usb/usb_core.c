#include "usb_core.h"
#include "ehci.h"
#include "uhci.h"
#include "../../../include/module/module_abi_v1.h"
#include "../../../libc/mem.h"
#include "../../../libc/string.h"

/**
 * USB Core Protocol Layer (Module)
 *
 * Controller-agnostic USB protocol implementation.
 * Supports both EHCI (USB 2.0 high-speed) and UHCI (USB 1.1 full/low-speed).
 * Routes transfers to the correct host controller based on which one
 * the device was enumerated from.
 */

// ============================================================================
// Global State
// ============================================================================

#define USB_MAX_DEVICES 16
static usb_device_t usb_devices[USB_MAX_DEVICES];
static int usb_num_devices = 0;
static uint8_t usb_next_address = 1;

/* Track which HC type each device is on */
#define HC_NONE 0
#define HC_EHCI 1
#define HC_UHCI 2
static uint8_t device_hc_type[USB_MAX_DEVICES];

static usb_class_driver_t *class_drivers[USB_MAX_CLASS_DRIVERS];
static int num_class_drivers = 0;

/* HC availability */
static int ehci_available = 0;
static int uhci_available = 0;

// ============================================================================
// Control Transfer (routes to correct HC driver)
// ============================================================================

int usb_control_msg(usb_device_t *dev, uint8_t request_type, uint8_t request,
                    uint16_t value, uint16_t index, void *data, uint16_t len) {
    usb_setup_packet_t setup;
    setup.bmRequestType = request_type;
    setup.bRequest      = request;
    setup.wValue        = value;
    setup.wIndex        = index;
    setup.wLength       = len;

    int direction = (request_type & USB_RT_DEV_TO_HOST) ? USB_DIR_IN : USB_DIR_OUT;

    /* Find which HC this device is on */
    int dev_idx = (int)(dev - usb_devices);
    if (dev_idx >= 0 && dev_idx < USB_MAX_DEVICES && device_hc_type[dev_idx] == HC_UHCI) {
        return uhci_control_transfer(dev->address, 0, &setup, data, len, direction);
    }
    return ehci_control_transfer(dev->address, 0, &setup, data, len, direction);
}

// ============================================================================
// Endpoint and Bulk Transfer Helpers
// ============================================================================

static usb_endpoint_t* usb_get_endpoint(usb_device_t *dev, uint8_t ep_addr) {
    for (int i = 0; i < dev->num_endpoints; i++) {
        if (dev->endpoints[i].address == ep_addr) {
            return &dev->endpoints[i];
        }
    }
    return NULL;
}

int usb_bulk_msg(usb_device_t *dev, uint8_t ep, void *data, uint16_t len, int direction) {
    int dev_idx = (int)(dev - usb_devices);
    if (dev_idx < 0 || dev_idx >= USB_MAX_DEVICES) return KABI_EINVAL;

    /* Get previous toggle state for this endpoint */
    usb_endpoint_t *ep_ptr = usb_get_endpoint(dev, ep);
    uint8_t toggle = 0;
    if (ep_ptr) {
        toggle = ep_ptr->toggle;
    }

    int result;
    if (device_hc_type[dev_idx] == HC_UHCI) {
        result = uhci_bulk_transfer(dev->address, ep, data, len, direction, &toggle);
    } else {
        result = ehci_bulk_transfer(dev->address, ep, data, len, direction, &toggle);
    }

    /* Save updated toggle state */
    if (result == KABI_SUCCESS && ep_ptr) {
        ep_ptr->toggle = toggle;
    }

    return result;
}

// ============================================================================
// Standard USB Requests
// ============================================================================

int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                       void *buf, uint16_t len) {
    return usb_control_msg(dev,
        USB_RT_DEV_TO_HOST | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        USB_REQ_GET_DESCRIPTOR,
        (type << 8) | index,
        0,
        buf, len);
}

int usb_set_address(usb_device_t *dev, uint8_t addr) {
    int rc = usb_control_msg(dev,
        USB_RT_HOST_TO_DEV | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        USB_REQ_SET_ADDRESS,
        addr,
        0,
        NULL, 0);

    if (rc == KABI_SUCCESS) {
        dev->address = addr;
    }
    return rc;
}

int usb_set_configuration(usb_device_t *dev, uint8_t config) {
    int rc = usb_control_msg(dev,
        USB_RT_HOST_TO_DEV | USB_RT_TYPE_STANDARD | USB_RT_RECIP_DEVICE,
        USB_REQ_SET_CONFIGURATION,
        config,
        0,
        NULL, 0);

    if (rc == KABI_SUCCESS) {
        dev->configured = 1;
    }
    return rc;
}

// ============================================================================
// Descriptor Parsing
// ============================================================================

static void usb_parse_config(usb_device_t *dev, uint8_t *buf, uint16_t total_len) {
    uint16_t offset = 0;

    while (offset < total_len) {
        uint8_t desc_len  = buf[offset];
        uint8_t desc_type = buf[offset + 1];

        if (desc_len == 0) break; /* Prevent infinite loop */

        if (desc_type == USB_DESC_INTERFACE && desc_len >= 9) {
            usb_interface_desc_t *iface = (usb_interface_desc_t *)&buf[offset];
            /* Store first interface's class info */
            if (iface->bInterfaceNumber == 0) {
                dev->if_class    = iface->bInterfaceClass;
                dev->if_subclass = iface->bInterfaceSubClass;
                dev->if_protocol = iface->bInterfaceProtocol;
                dev->if_number   = iface->bInterfaceNumber;
            }
        }

        if (desc_type == USB_DESC_ENDPOINT && desc_len >= 7) {
            usb_endpoint_desc_t *ep = (usb_endpoint_desc_t *)&buf[offset];
            if (dev->num_endpoints < USB_MAX_ENDPOINTS) {
                usb_endpoint_t *e = &dev->endpoints[dev->num_endpoints++];
                e->address    = ep->bEndpointAddress;
                e->type       = ep->bmAttributes & 0x03;
                e->max_packet = ep->wMaxPacketSize;
                e->interval   = ep->bInterval;
            }
        }

        offset += desc_len;
    }
}

// ============================================================================
// Class Driver Framework
// ============================================================================

int usb_register_class_driver(usb_class_driver_t *driver) {
    if (num_class_drivers >= USB_MAX_CLASS_DRIVERS) return KABI_ENOMEM;
    class_drivers[num_class_drivers++] = driver;
    kprint("[USB] Registered class driver: ");
    kprint((char *)driver->name);
    kprint("\n");
    return KABI_SUCCESS;
}

static void usb_probe_device(usb_device_t *dev) {
    for (int i = 0; i < num_class_drivers; i++) {
        usb_class_driver_t *drv = class_drivers[i];

        /* Match on interface class/subclass/protocol */
        if (drv->match_class    == dev->if_class &&
            drv->match_subclass == dev->if_subclass &&
            drv->match_protocol == dev->if_protocol) {
            kprint("[USB] Matched driver '");
            kprint((char *)drv->name);
            kprint("' for device\n");
            if (drv->probe && drv->probe(dev) == KABI_SUCCESS) {
                return; /* Driver claimed the device */
            }
        }
    }
    kprint("[USB] No driver matched for device class=0x");
    char s[16];
    kabi_hex_to_ascii(dev->if_class, s); kprint(s);
    kprint(" sub=0x");
    kabi_hex_to_ascii(dev->if_subclass, s); kprint(s);
    kprint(" proto=0x");
    kabi_hex_to_ascii(dev->if_protocol, s); kprint(s);
    kprint("\n");
}

// ============================================================================
// Device Enumeration (parameterized by HC type)
// ============================================================================

static int usb_enumerate_device_on_hc(int port, int hc_type) {
    if (usb_num_devices >= USB_MAX_DEVICES) {
        kprint("[USB] Max devices reached\n");
        return KABI_ENOMEM;
    }

    char s[16];
    kprint("[USB] Enumerating device on ");
    kprint(hc_type == HC_EHCI ? "EHCI" : "UHCI");
    kprint(" port ");
    kabi_int_to_ascii(port, s); kprint(s); kprint("\n");

    /* Reset the port */
    if (hc_type == HC_EHCI) {
        ehci_port_reset(port);
    } else {
        uhci_port_reset(port);
    }

    /* Check if device is still connected */
    uint32_t portsc;
    if (hc_type == HC_EHCI) {
        portsc = ehci_port_status(port);
        if (!(portsc & PORTSC_CONNECTED)) {
            kprint("[USB] No device after port reset\n");
            return KABI_ENOENT;
        }
    } else {
        portsc = uhci_port_status(port);
        if (!(portsc & UHCI_PORT_CONNECTED)) {
            kprint("[USB] No device after port reset\n");
            return KABI_ENOENT;
        }
    }

    /* Create device */
    int dev_idx = usb_num_devices;
    usb_device_t *dev = &usb_devices[dev_idx];
    memory_set((uint8_t *)dev, 0, sizeof(usb_device_t));
    dev->address = 0;
    dev->port = port;
    device_hc_type[dev_idx] = hc_type;

    if (hc_type == HC_EHCI) {
        dev->speed = (portsc & PORTSC_ENABLED) ? USB_SPEED_HIGH : USB_SPEED_FULL;
    } else {
        dev->speed = (portsc & UHCI_PORT_LOW_SPEED) ? USB_SPEED_LOW : USB_SPEED_FULL;
    }

    /* Step 1: Get first 8 bytes of device descriptor */
    usb_device_desc_t dev_desc;
    memory_set((uint8_t *)&dev_desc, 0, sizeof(dev_desc));

    int rc = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev_desc, 8);
    if (rc != KABI_SUCCESS) {
        kprint("[USB] Failed to get device descriptor\n");
        return rc;
    }

    dev->max_packet_0 = dev_desc.bMaxPacketSize0;
    if (dev->max_packet_0 == 0) dev->max_packet_0 = 8;

    /* Step 2: Assign unique address */
    uint8_t new_addr = usb_next_address++;
    rc = usb_set_address(dev, new_addr);
    if (rc != KABI_SUCCESS) {
        kprint("[USB] Failed to set address\n");
        return rc;
    }

    kprint("[USB] Assigned address ");
    kabi_int_to_ascii(new_addr, s); kprint(s); kprint("\n");

    /* USB spec: 2ms delay after address */
    volatile uint32_t delay = 50000;
    while (delay--) asm volatile("nop");

    /* Step 3: Get full device descriptor */
    rc = usb_get_descriptor(dev, USB_DESC_DEVICE, 0, &dev_desc, sizeof(dev_desc));
    if (rc != KABI_SUCCESS) {
        kprint("[USB] Failed to get full device descriptor\n");
        return rc;
    }

    dev->vendor_id       = dev_desc.idVendor;
    dev->product_id      = dev_desc.idProduct;
    dev->bcd_device      = dev_desc.bcdDevice;
    dev->device_class    = dev_desc.bDeviceClass;
    dev->device_subclass = dev_desc.bDeviceSubClass;
    dev->device_protocol = dev_desc.bDeviceProtocol;

    kprint("[USB] Device: vendor=0x");
    kabi_hex_to_ascii(dev->vendor_id, s); kprint(s);
    kprint(" product=0x");
    kabi_hex_to_ascii(dev->product_id, s); kprint(s);
    kprint(" class=0x");
    kabi_hex_to_ascii(dev->device_class, s); kprint(s);
    kprint("\n");

    /* Step 4: Get configuration descriptor */
    usb_config_desc_t cfg_desc;
    rc = usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, &cfg_desc, sizeof(cfg_desc));
    if (rc != KABI_SUCCESS) {
        kprint("[USB] Failed to get config descriptor\n");
    } else {
        uint16_t total = cfg_desc.wTotalLength;
        if (total > 256) total = 256;
        uint8_t *cfg_buf = (uint8_t *)kmalloc(total, 0, 0);
        if (cfg_buf) {
            rc = usb_get_descriptor(dev, USB_DESC_CONFIGURATION, 0, cfg_buf, total);
            if (rc == KABI_SUCCESS) {
                usb_parse_config(dev, cfg_buf, total);

                kprint("[USB] Interface: class=0x");
                kabi_hex_to_ascii(dev->if_class, s); kprint(s);
                kprint(" sub=0x");
                kabi_hex_to_ascii(dev->if_subclass, s); kprint(s);
                kprint(" proto=0x");
                kabi_hex_to_ascii(dev->if_protocol, s); kprint(s);
                kprint(" endpoints=");
                kabi_int_to_ascii(dev->num_endpoints, s); kprint(s);
                kprint("\n");
            }
            kfree(cfg_buf);
        }

        usb_set_configuration(dev, cfg_desc.bConfigurationValue);
    }

    usb_num_devices++;

    /* Step 6: Probe class drivers */
    usb_probe_device(dev);

    return KABI_SUCCESS;
}

int usb_enumerate_device(int port) {
    return usb_enumerate_device_on_hc(port, HC_EHCI);
}

// ============================================================================
// Device Lookup
// ============================================================================

usb_device_t *usb_get_device(int index) {
    if (index < 0 || index >= usb_num_devices) return NULL;
    return &usb_devices[index];
}

int usb_device_count(void) {
    return usb_num_devices;
}

// ============================================================================
// Init — tries both EHCI and UHCI
// ============================================================================

int usb_core_init(void) {
    kprint("[USB] USB Core initializing...\n");

    usb_num_devices = 0;
    usb_next_address = 1;
    /* NOTE: Do NOT reset num_class_drivers here — class drivers
     * (e.g. mass_storage) are registered BEFORE usb_core_init() */
    memory_set((uint8_t *)usb_devices, 0, sizeof(usb_devices));
    memory_set((uint8_t *)device_hc_type, 0, sizeof(device_hc_type));

    /* Try EHCI first (USB 2.0 high-speed) */
    if (ehci_init() == KABI_SUCCESS) {
        ehci_available = 1;
        int nports = ehci_port_count();
        for (int i = 0; i < nports; i++) {
            uint32_t portsc = ehci_port_status(i);
            if (portsc & PORTSC_CONNECTED) {
                usb_enumerate_device_on_hc(i, HC_EHCI);
            }
        }
    }

    /* Try UHCI (USB 1.1 full/low-speed) */
    if (uhci_init() == KABI_SUCCESS) {
        uhci_available = 1;
        int nports = uhci_port_count();
        for (int i = 0; i < nports; i++) {
            uint32_t portsc = uhci_port_status(i);
            if (portsc & UHCI_PORT_CONNECTED) {
                usb_enumerate_device_on_hc(i, HC_UHCI);
            }
        }
    }

    if (!ehci_available && !uhci_available) {
        kprint("[USB] No USB host controllers found\n");
        return KABI_ENOENT;
    }

    char s[16];
    kabi_int_to_ascii(usb_num_devices, s);
    kprint("[USB] Enumeration complete: ");
    kprint(s);
    kprint(" device(s) found\n");

    return KABI_SUCCESS;
}

/* --- Module Registration --- */
static int usb_core_module_init(void) { return usb_core_init(); }

kabi_module_t __kabi_module_usb_core = {
    .name           = "usb_core",
    .abi_version    = MODULE_ABI_V1_0,
    .module_version = 0x0200,
    .init           = usb_core_module_init,
    .exit           = NULL,
    .description    = "USB protocol layer (EHCI + UHCI, device enumeration, class driver framework)"
};

