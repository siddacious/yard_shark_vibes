//
// tusb_config.h
//
// TinyUSB configuration for the Pico 2 WebUSB SPI uploader.  We
// configure the device as a full‑speed MCU with a single vendor
// interface and only bulk endpoints.  This header is included by
// TinyUSB and should be kept in the include path for the project.

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "pico/stdlib.h"

// MCU and OS selection.  The RP2040 and RP2350 share the same
// TinyUSB configuration as far as the device stack is concerned.
#define CFG_TUSB_MCU                 OPT_MCU_RP2040
#define CFG_TUSB_OS                  OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE        (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Alignment for USB transfer buffers.  Align to 4 bytes to satisfy
// DMA requirements.
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN           __attribute__ ((aligned(4)))

// Endpoint zero size
#define CFG_TUD_ENDPOINT0_SIZE       64

// Enable the vendor interface and configure buffer sizes.  The
// receive buffer is large enough to hold a 4 KiB flash page.  The
// transmit buffer is small since we only ever send short status
// responses.
#define CFG_TUD_VENDOR               1
#define CFG_TUD_VENDOR_RX_BUFSIZE    4096
#define CFG_TUD_VENDOR_TX_BUFSIZE    512

// Disable other classes
#define CFG_TUD_CDC                  0
#define CFG_TUD_MSC                  0
#define CFG_TUD_HID                  0
#define CFG_TUD_MIDI                 0
#define CFG_TUD_NET                  0
#define CFG_TUD_DFU_RT               0

#endif // TUSB_CONFIG_H