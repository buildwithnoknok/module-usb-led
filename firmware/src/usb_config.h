#ifndef _USB_CONFIG_H
#define _USB_CONFIG_H

#include "funconfig.h"
#include "ch32fun.h"

// ── Endpoint config ───────────────────────────────────────────────────────────
// EP0: control (built-in)
// EP1: CDC data IN  (device → host, for responses)
// EP2: CDC data OUT (host → device, for commands)
// EP3: CDC notification IN (required by CDC spec, we send nothing here)
#define FUSB_CONFIG_EPS       4
#define FUSB_EP1_MODE         1   // TX (IN)
#define FUSB_EP2_MODE        -1   // RX (OUT)
#define FUSB_EP3_MODE         1   // TX (IN) — CDC notification, unused
#define FUSB_SUPPORTS_SLEEP   0
#define FUSB_HID_INTERFACES   0
#define FUSB_CURSED_TURBO_DMA 0
#define FUSB_HID_USER_REPORTS 0
#define FUSB_IO_PROFILE       0
#define FUSB_USE_HPE          FUNCONF_ENABLE_HPE
#define FUSB_USER_HANDLERS    1
#define FUSB_USE_DMA7_COPY    0
#define FUSB_VDD_5V           0

#include "usb_defines.h"

// ── USB identity ─────────────────────────────────────────────────────────────
// NOTE: Replace VID/PID before production release
#define FUSB_USB_VID  0x1209          // pid.codes (open-source VID for prototypes)
#define FUSB_USB_PID  0x4E4E          // "NN" — noknok LEDs module
#define FUSB_USB_REV  0x0100          // v1.0
#define FUSB_STR_MANUFACTURER  u"noknok"
#define FUSB_STR_PRODUCT       u"noknok LEDs"
#define FUSB_STR_SERIAL        u"00000001"

// ── Device descriptor ─────────────────────────────────────────────────────────
static const uint8_t device_descriptor[] = {
    18,                                         // bLength
    1,                                          // bDescriptorType (Device)
    0x10, 0x01,                                 // bcdUSB 1.1
    0x02,                                       // bDeviceClass: CDC
    0x00,                                       // bDeviceSubClass
    0x00,                                       // bDeviceProtocol
    64,                                         // bMaxPacketSize0
    (uint8_t)(FUSB_USB_VID),
    (uint8_t)(FUSB_USB_VID >> 8),
    (uint8_t)(FUSB_USB_PID),
    (uint8_t)(FUSB_USB_PID >> 8),
    (uint8_t)(FUSB_USB_REV),
    (uint8_t)(FUSB_USB_REV >> 8),
    1,                                          // iManufacturer
    2,                                          // iProduct
    3,                                          // iSerialNumber
    1,                                          // bNumConfigurations
};

// ── Configuration descriptor ──────────────────────────────────────────────────
// Total size: 9 (config) + 9 (IAD) + 9 (ctrl intf) + 5 (CDC header) +
//             5 (CDC call mgmt) + 4 (CDC ACM) + 5 (CDC union) + 7 (EP3) +
//             9 (data intf) + 7 (EP1) + 7 (EP2) = 75 bytes
static const uint8_t config_descriptor[] = {
    // Configuration
    0x09, 0x02, 0x4B, 0x00,  // bLength, bType, wTotalLength=75
    0x02,                    // bNumInterfaces (2: CDC control + CDC data)
    0x01,                    // bConfigurationValue
    0x00,                    // iConfiguration
    0x80,                    // bmAttributes: bus-powered
    0x32,                    // bMaxPower: 100 mA

    // Interface Association Descriptor (IAD) — groups the two CDC interfaces
    0x08, 0x0B,              // bLength=8, bDescriptorType=IAD
    0x00,                    // bFirstInterface=0
    0x02,                    // bInterfaceCount=2
    0x02,                    // bFunctionClass: CDC
    0x02,                    // bFunctionSubClass: ACM
    0x01,                    // bFunctionProtocol: AT
    0x00,                    // iFunction

    // Interface 0: CDC Control
    0x09, 0x04,              // bLength=9, bDescriptorType=Interface
    0x00,                    // bInterfaceNumber=0
    0x00,                    // bAlternateSetting=0
    0x01,                    // bNumEndpoints=1
    0x02,                    // bInterfaceClass: CDC
    0x02,                    // bInterfaceSubClass: ACM
    0x01,                    // bInterfaceProtocol: AT commands
    0x00,                    // iInterface

    // CDC Header Functional Descriptor
    0x05, 0x24, 0x00,        // bLength=5, bType=CS_INTERFACE, bSubtype=Header
    0x10, 0x01,              // bcdCDC=1.1

    // CDC Call Management Functional Descriptor
    0x05, 0x24, 0x01,        // bLength=5, bType=CS_INTERFACE, bSubtype=CallMgmt
    0x00,                    // bmCapabilities: no call management
    0x01,                    // bDataInterface=1

    // CDC ACM Functional Descriptor
    0x04, 0x24, 0x02,        // bLength=4, bType=CS_INTERFACE, bSubtype=ACM
    0x02,                    // bmCapabilities: supports Set_Line_Coding

    // CDC Union Functional Descriptor
    0x05, 0x24, 0x06,        // bLength=5, bType=CS_INTERFACE, bSubtype=Union
    0x00,                    // bControlInterface=0
    0x01,                    // bSubordinateInterface=1

    // Endpoint 3: CDC Notification IN (interrupt, unused but required by spec)
    0x07, 0x05,              // bLength=7, bDescriptorType=Endpoint
    0x83,                    // bEndpointAddress: IN EP3
    0x03,                    // bmAttributes: Interrupt
    0x08, 0x00,              // wMaxPacketSize=8
    0xFF,                    // bInterval: 255ms

    // Interface 1: CDC Data
    0x09, 0x04,              // bLength=9, bDescriptorType=Interface
    0x01,                    // bInterfaceNumber=1
    0x00,                    // bAlternateSetting=0
    0x02,                    // bNumEndpoints=2
    0x0A,                    // bInterfaceClass: CDC Data
    0x00,                    // bInterfaceSubClass
    0x00,                    // bInterfaceProtocol
    0x00,                    // iInterface

    // Endpoint 1: CDC Data IN (device → host, responses)
    0x07, 0x05,              // bLength=7, bDescriptorType=Endpoint
    0x81,                    // bEndpointAddress: IN EP1
    0x02,                    // bmAttributes: Bulk
    0x40, 0x00,              // wMaxPacketSize=64
    0x00,                    // bInterval

    // Endpoint 2: CDC Data OUT (host → device, commands)
    0x07, 0x05,              // bLength=7, bDescriptorType=Endpoint
    0x02,                    // bEndpointAddress: OUT EP2
    0x02,                    // bmAttributes: Bulk
    0x40, 0x00,              // wMaxPacketSize=64
    0x00,                    // bInterval
};

// ── String descriptors ────────────────────────────────────────────────────────
struct usb_string_descriptor_struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wString[];
};

static const struct usb_string_descriptor_struct string0
    __attribute__((section(".rodata"))) = { 4, 3, {0x0409} };
static const struct usb_string_descriptor_struct string1
    __attribute__((section(".rodata"))) = { sizeof(FUSB_STR_MANUFACTURER), 3, FUSB_STR_MANUFACTURER };
static const struct usb_string_descriptor_struct string2
    __attribute__((section(".rodata"))) = { sizeof(FUSB_STR_PRODUCT),      3, FUSB_STR_PRODUCT };
static const struct usb_string_descriptor_struct string3
    __attribute__((section(".rodata"))) = { sizeof(FUSB_STR_SERIAL),       3, FUSB_STR_SERIAL };

// ── Descriptor lookup table ───────────────────────────────────────────────────
static const struct descriptor_list_struct {
    uint32_t       lIndexValue;
    const uint8_t *addr;
    uint8_t        length;
} descriptor_list[] = {
    { 0x00000100, device_descriptor,      sizeof(device_descriptor) },
    { 0x00000200, config_descriptor,      sizeof(config_descriptor) },
    { 0x00000300, (const uint8_t *)&string0, 4 },
    { 0x04090301, (const uint8_t *)&string1, string1.bLength },
    { 0x04090302, (const uint8_t *)&string2, string2.bLength },
    { 0x04090303, (const uint8_t *)&string3, string3.bLength },
};
#define DESCRIPTOR_LIST_ENTRIES \
    ((sizeof(descriptor_list)) / (sizeof(struct descriptor_list_struct)))

#endif
