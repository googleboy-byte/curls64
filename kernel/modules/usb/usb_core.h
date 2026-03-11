#ifndef USB_CORE_H
#define USB_CORE_H

#include <stdint.h>

/**
 * USB Core Protocol Layer (Module)
 * 
 * Controller-agnostic USB 2.0 protocol implementation.
 * Provides device enumeration, descriptor parsing, and a class driver
 * registration framework. The mass storage driver (and future class
 * drivers) interact exclusively through this layer.
 *
 * Architecture:
 *   [Class Drivers]  <-- mass_storage, HID, etc.
 *   [USB Core]       <-- you are here
 *   [HC Drivers]     <-- EHCI, UHCI, OHCI, xHCI
 *
 * Helper Libraries:
 *   Kernel-space: libc/ (string.h, mem.h, kheap.h)
 *   See include/module/module_abi_v1.h for full K-ABI reference.
 */

/* --- Transfer directions --- */
#define USB_DIR_OUT     0
#define USB_DIR_IN      1

/* --- USB Speeds --- */
#define USB_SPEED_LOW   0
#define USB_SPEED_FULL  1
#define USB_SPEED_HIGH  2

/* --- Standard Request Types (bmRequestType) --- */
#define USB_RT_HOST_TO_DEV      0x00
#define USB_RT_DEV_TO_HOST      0x80
#define USB_RT_TYPE_STANDARD    0x00
#define USB_RT_TYPE_CLASS       0x20
#define USB_RT_TYPE_VENDOR      0x40
#define USB_RT_RECIP_DEVICE     0x00
#define USB_RT_RECIP_INTERFACE  0x01
#define USB_RT_RECIP_ENDPOINT   0x02

/* --- Standard Request Codes (bRequest) --- */
#define USB_REQ_GET_STATUS         0x00
#define USB_REQ_CLEAR_FEATURE      0x01
#define USB_REQ_SET_FEATURE        0x03
#define USB_REQ_SET_ADDRESS        0x05
#define USB_REQ_GET_DESCRIPTOR     0x06
#define USB_REQ_SET_DESCRIPTOR     0x07
#define USB_REQ_GET_CONFIGURATION  0x08
#define USB_REQ_SET_CONFIGURATION  0x09
#define USB_REQ_GET_INTERFACE      0x0A
#define USB_REQ_SET_INTERFACE      0x0B

/* --- Descriptor Types --- */
#define USB_DESC_DEVICE          0x01
#define USB_DESC_CONFIGURATION   0x02
#define USB_DESC_STRING          0x03
#define USB_DESC_INTERFACE       0x04
#define USB_DESC_ENDPOINT        0x05

/* --- USB Class Codes --- */
#define USB_CLASS_MASS_STORAGE   0x08
#define USB_CLASS_HID            0x03
#define USB_CLASS_HUB            0x09

/* Mass Storage subclasses/protocols */
#define USB_MSC_SUBCLASS_SCSI    0x06
#define USB_MSC_PROTOCOL_BBB     0x50  /* Bulk-Only Transport */

/* --- Endpoint Types --- */
#define USB_EP_TYPE_CONTROL      0x00
#define USB_EP_TYPE_ISOCHRONOUS  0x01
#define USB_EP_TYPE_BULK         0x02
#define USB_EP_TYPE_INTERRUPT    0x03

/* --- Setup Packet (8 bytes, per USB 2.0 spec) --- */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

/* --- Device Descriptor (18 bytes) --- */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* --- Configuration Descriptor (9 bytes) --- */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

/* --- Interface Descriptor (9 bytes) --- */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_desc_t;

/* --- Endpoint Descriptor (7 bytes) --- */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* --- Endpoint info (parsed) --- */
typedef struct {
    uint8_t  address;       /* Endpoint address (with direction bit) */
    uint8_t  type;          /* USB_EP_TYPE_* */
    uint16_t max_packet;    /* Max packet size */
    uint8_t  interval;      /* Polling interval */
    uint8_t  toggle;        /* Data toggle (0 or 1) */
} usb_endpoint_t;

/* --- USB Device (runtime state) --- */
#define USB_MAX_ENDPOINTS 16

typedef struct usb_device {
    uint8_t  address;           /* Assigned USB address (1-127) */
    uint8_t  speed;             /* USB_SPEED_* */
    uint8_t  port;              /* Root hub port number */
    int      configured;        /* Has SET_CONFIGURATION been sent? */

    /* From device descriptor */
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t bcd_device;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_0;      /* Max packet size for EP0 */

    /* From interface descriptor (first interface) */
    uint8_t  if_class;
    uint8_t  if_subclass;
    uint8_t  if_protocol;
    uint8_t  if_number;

    /* Endpoints */
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint8_t  num_endpoints;

    void     *driver_data;      /* Opaque pointer for class drivers */
} usb_device_t;

/* --- Class Driver Registration --- */
#define USB_MAX_CLASS_DRIVERS 8

typedef struct {
    const char *name;
    uint8_t  match_class;
    uint8_t  match_subclass;
    uint8_t  match_protocol;
    int      (*probe)(usb_device_t *dev);
    void     (*disconnect)(usb_device_t *dev);
} usb_class_driver_t;

/* --- Public API --- */

/* Initialization */
int  usb_core_init(void);

/* Device enumeration */
int  usb_enumerate_device(int port);

/* Control transfers */
int  usb_control_msg(usb_device_t *dev, uint8_t request_type, uint8_t request,
                     uint16_t value, uint16_t index, void *data, uint16_t len);

/* Bulk transfers */
int  usb_bulk_msg(usb_device_t *dev, uint8_t ep, void *data, uint16_t len, int direction);

/* Standard requests */
int  usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                        void *buf, uint16_t len);
int  usb_set_address(usb_device_t *dev, uint8_t addr);
int  usb_set_configuration(usb_device_t *dev, uint8_t config);

/* Class driver framework */
int  usb_register_class_driver(usb_class_driver_t *driver);

/* Device lookup */
usb_device_t *usb_get_device(int index);
int  usb_device_count(void);

#endif
