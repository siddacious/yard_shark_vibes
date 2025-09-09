//
// usb_descriptors.c
//
// TinyUSB descriptor implementation for a vendor‑specific device.  We
// expose a single interface with two bulk endpoints (OUT and IN).
// Additional descriptors are provided for WebUSB and Microsoft OS
// 2.0 to allow driverless operation on Windows and easy access from
// web pages.

#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>

// Dummy VID/PID pair for development.  Replace with your own if
// shipping hardware.
#define USB_VID 0xCafe
#define USB_PID 0x4001

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+

static tusb_desc_device_t const desc_device = {
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,
  .bDeviceClass       = 0x00,
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,
  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor (Full speed)
//--------------------------------------------------------------------+

// Interface numbers
enum {
  ITF_NUM_VENDOR,
  ITF_NUM_TOTAL
};

// Endpoint addresses (bEndpointAddress)
#define EPNUM_VENDOR_OUT   0x01
#define EPNUM_VENDOR_IN    0x81

// Full‑speed configuration descriptor.  This structure defines a
// single vendor class interface with two bulk endpoints.  The total
// length (wTotalLength) is set to 9 + (9 + 7 + 7) bytes.
static uint8_t const desc_fs_configuration[] = {
  // Configuration descriptor header
  9, TUSB_DESC_CONFIGURATION,
  9 + (9 + 7 + 7), 0x00, // total length
  0x01,                  // num interfaces
  0x01,                  // configuration value
  0x00,                  // iConfiguration
  0x80,                  // attributes: bus powered
  50,                    // max power: 100 mA

  // Interface descriptor: vendor‑specific class
  9, TUSB_DESC_INTERFACE,
  ITF_NUM_VENDOR,
  0x00,                  // alternate setting
  0x02,                  // number of endpoints
  0xFF, 0x00, 0x00,      // class, subclass, protocol
  0x00,                  // iInterface

  // Endpoint descriptor (OUT, bulk)
  7, TUSB_DESC_ENDPOINT,
  EPNUM_VENDOR_OUT,
  TUSB_XFER_BULK,
  64, 0x00,             // packet size
  0x00,                 // interval (ignored for bulk)

  // Endpoint descriptor (IN, bulk)
  7, TUSB_DESC_ENDPOINT,
  EPNUM_VENDOR_IN,
  TUSB_XFER_BULK,
  64, 0x00,
  0x00,
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// BOS Descriptor with WebUSB and Microsoft OS 2.0 descriptors
//--------------------------------------------------------------------+

#define WEBUSB_UUID  {0x38,0xB6,0x08,0x34,0xA9,0x09,0xA0,0x47,0x8B,0xFD,0xA0,0x76,0x88,0x15,0xB6,0x65}
#define MS_OS_20_UUID {0xDF,0x60,0xDD,0xD8,0x89,0x45,0xC7,0x4C,0x9C,0xD2,0x65,0x9D,0x9E,0x64,0x8A,0x9F}

// BOS descriptor advertising WebUSB and WinUSB capabilities.  The
// landing page and vendor codes are left zero in this example.  For
// a full WebUSB implementation you would add a WebUSB URL descriptor
// and handle vendor requests.  For Windows driverless support you
// would also supply a Microsoft OS 2.0 descriptor set.
uint8_t const desc_bos[] = {
  0x05, TUSB_DESC_BOS, 0x16, 0x00, 0x02, // BOS header (2 capabilities)
  // WebUSB platform capability
  0x18, TUSB_DESC_DEVICE_CAPABILITY, 0x05, 0x00,
  WEBUSB_UUID, 0x00, 0x01, // bcdVersion
  0x00, 0x00,              // vendor code + landing page
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  // Microsoft OS 2.0 platform capability
  0x1C, TUSB_DESC_DEVICE_CAPABILITY, 0x05, 0x00,
  MS_OS_20_UUID, 0x00, 0x00,
  0x03, 0x06,              // Windows version (0x06030000 = Windows 8.1)
  0xB2, 0x00,              // descriptor set length (178 bytes)
  0x00, 0x00,              // vendor code
  0x00, 0x00               // alternate enumeration code
};

uint8_t const * tud_descriptor_bos_cb(void) {
  return desc_bos;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

static char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

char const* string_desc_arr[] = {
  (const char[]){ 0x09, 0x04 }, // 0: supported language (English)
  "Open Bulk SPI Uploader",    // 1: Manufacturer
  "Pico2 WebUSB SPI",          // 2: Product
  serial_str                    // 3: Serial number (unique per board)
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  uint8_t chr_count;

  // index 0 returns the language ID
  if (index == 0) {
    _desc_str[1] = 0x0409; // English
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * 1 + 2);
    return _desc_str;
  }

  // Update serial number at runtime
  if (index == 3) {
    pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
  }

  const char* str = string_desc_arr[index];
  chr_count = (uint8_t) strlen(str);
  if (chr_count > 31) chr_count = 31;
  for (uint8_t i = 0; i < chr_count; i++) {
    _desc_str[1 + i] = str[i];
  }
  _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
  return _desc_str;
}