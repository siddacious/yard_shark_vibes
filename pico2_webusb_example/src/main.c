//
// main.c
//
// Firmware for the Raspberry Pi Pico 2 (RP2350) exposing a TinyUSB
// vendor bulk interface.  Incoming data is written directly to an
// attached SPI flash chip.  The protocol is intentionally simple:
// the host first sends an 8‑byte header consisting of the four
// characters 'F','W','U','P' followed by a little‑endian 32‑bit
// total byte count.  The device erases the flash region starting at
// address 0 that spans the incoming payload and then programs data
// pages as chunks arrive over the USB bulk endpoint.

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "tusb.h"

// ======= User configuration =======
// Adjust these pins to match your wiring.  We use SPI0 on pins
// GP2/GP3/GP4 with CS on GP5.  The baud rate of 10 MHz is a safe
// starting point for most 3.3 V SPI NOR flash devices.
#define FLASH_SPI             spi0
#define FLASH_BAUD            (10 * 1000 * 1000)  // 10 MHz
#define PIN_SPI_SCK           2
#define PIN_SPI_MOSI          3
#define PIN_SPI_MISO          4
#define PIN_FLASH_CS          5

// JEDEC command opcodes for a generic 4‑KiB erase SPI flash.  If your
// device requires 64‑KiB erases, you may need to change
// CMD_SECTOR_ERASE_4K and adjust the erase logic accordingly.
#define CMD_WREN              0x06
#define CMD_RDSR              0x05
#define CMD_PP                0x02
#define CMD_SECTOR_ERASE_4K   0x20
#define CMD_RDID              0x9F

// Page and sector sizes for common SPI NOR flashes.
#define PAGE_SIZE             256
#define SECTOR_SIZE           4096

// Protocol header constants.  The host sends these four ASCII
// characters followed by a 32‑bit little‑endian total size.
static const uint8_t PROTO_MAGIC[4] = {'F','W','U','P'};

// Global state for the current upload session
static uint32_t expected_total = 0;
static uint32_t received_total = 0;
static uint32_t write_addr = 0;
static bool header_received = false;

// Helper to assert the chip select line
static inline void cs_low(void)  { gpio_put(PIN_FLASH_CS, 0); }
static inline void cs_high(void) { gpio_put(PIN_FLASH_CS, 1); }

// Initialise SPI and CS GPIOs
static void flash_spi_init(void) {
  spi_init(FLASH_SPI, FLASH_BAUD);
  gpio_set_function(PIN_SPI_SCK,  GPIO_FUNC_SPI);
  gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);

  gpio_init(PIN_FLASH_CS);
  gpio_set_dir(PIN_FLASH_CS, GPIO_OUT);
  cs_high();
}

// Send Write Enable (WREN) command
static void flash_write_enable(void) {
  uint8_t cmd = CMD_WREN;
  cs_low();
  spi_write_blocking(FLASH_SPI, &cmd, 1);
  cs_high();
}

// Read Status Register (RDSR) to check busy flag
static uint8_t flash_read_status(void) {
  uint8_t cmd = CMD_RDSR;
  uint8_t sr = 0;
  cs_low();
  spi_write_blocking(FLASH_SPI, &cmd, 1);
  spi_read_blocking(FLASH_SPI, 0xFF, &sr, 1);
  cs_high();
  return sr;
}

// Wait until the flash is no longer busy.  Busy is indicated by
// status bit 0 set.
static void flash_wait_busy(void) {
  while (flash_read_status() & 0x01) {
    tight_loop_contents();
  }
}

// Read the JEDEC ID (3 bytes).  Useful for debug.
static void flash_read_jedec(uint8_t id[3]) {
  uint8_t cmd = CMD_RDID;
  cs_low();
  spi_write_blocking(FLASH_SPI, &cmd, 1);
  spi_read_blocking(FLASH_SPI, 0xFF, id, 3);
  cs_high();
}

// Erase a 4‑KiB sector at the given address.  Assumes address is
// sector‑aligned.  If your device requires larger erase units,
// update CMD_SECTOR_ERASE_4K accordingly.
static void flash_sector_erase(uint32_t addr) {
  flash_write_enable();
  uint8_t cmd[4] = { CMD_SECTOR_ERASE_4K,
                     (uint8_t)(addr >> 16),
                     (uint8_t)(addr >> 8),
                     (uint8_t)(addr) };
  cs_low();
  spi_write_blocking(FLASH_SPI, cmd, sizeof(cmd));
  cs_high();
  flash_wait_busy();
}

// Program a page (up to 256 bytes) starting at `addr`.  Does not
// cross a page boundary.  The caller must ensure that the area has
// been erased and that writes stay within the same 256‑byte page.
static void flash_page_program(uint32_t addr, const uint8_t *data, uint32_t len) {
  flash_write_enable();
  uint8_t hdr[4] = { CMD_PP,
                     (uint8_t)(addr >> 16),
                     (uint8_t)(addr >> 8),
                     (uint8_t)(addr) };
  cs_low();
  spi_write_blocking(FLASH_SPI, hdr, sizeof(hdr));
  spi_write_blocking(FLASH_SPI, data, len);
  cs_high();
  flash_wait_busy();
}

// Erase enough 4 KiB sectors to cover the range [start, start + size).
static void erase_range_4k_aligned(uint32_t start, uint32_t size) {
  uint32_t a0 = start & ~(SECTOR_SIZE - 1);
  uint32_t a1 = (start + size + SECTOR_SIZE - 1) & ~(SECTOR_SIZE - 1);
  for (uint32_t a = a0; a < a1; a += SECTOR_SIZE) {
    flash_sector_erase(a);
  }
}

// Program a sequence of bytes, taking care not to cross page
// boundaries.  write_addr is updated globally.
static void program_stream(const uint8_t *buf, uint32_t len) {
  uint32_t off = 0;
  while (off < len) {
    uint32_t page_off = write_addr & (PAGE_SIZE - 1);
    uint32_t chunk = PAGE_SIZE - page_off;
    if (chunk > (len - off)) chunk = len - off;
    flash_page_program(write_addr, buf + off, chunk);
    write_addr += chunk;
    off += chunk;
  }
}

// Reset the current upload session
static void reset_session(void) {
  expected_total = 0;
  received_total = 0;
  write_addr = 0;
  header_received = false;
}

// Handle incoming bulk data.  This is called repeatedly from the
// TinyUSB task.  It reads available bytes from the vendor OUT
// endpoint, interprets the first packet as a header, then programs
// subsequent data into flash.
static void handle_vendor_out(void) {
  uint8_t tmp[4096];
  while (tud_vendor_available()) {
    uint32_t n = tud_vendor_read(tmp, sizeof(tmp));
    if (!header_received) {
      // Require at least 8 bytes for the magic + length
      if (n < 8) {
        reset_session();
        return;
      }
      if (memcmp(tmp, PROTO_MAGIC, 4) != 0) {
        reset_session();
        return;
      }
      expected_total = ((uint32_t)tmp[4]) |
                       ((uint32_t)tmp[5] << 8) |
                       ((uint32_t)tmp[6] << 16) |
                       ((uint32_t)tmp[7] << 24);
      // Erase the flash area
      erase_range_4k_aligned(0, expected_total);
      header_received = true;
      // Process any remaining payload in this first packet
      uint32_t remain = n - 8;
      if (remain) {
        program_stream(tmp + 8, remain);
        received_total += remain;
      }
    } else {
      program_stream(tmp, n);
      received_total += n;
    }
    // If we've received all expected bytes, send an "OK" back
    if (received_total >= expected_total && expected_total != 0) {
      const char ok[] = "OK";
      tud_vendor_write_str(ok);
      tud_vendor_flush();
    }
  }
}

int main(void) {
  // Initialise standard I/O (for optional debug) and the SPI flash
  stdio_init_all();
  flash_spi_init();

  // Read the JEDEC ID for informational purposes.  If you have a
  // console connected via CDC, you can print id[0..2] here to check
  // that your flash is talking.
  uint8_t id[3] = {0};
  flash_read_jedec(id);

  // Initialise TinyUSB device layer
  tusb_init();

  while (true) {
    // TinyUSB device task
    tud_task();
    // When connected and configured, process incoming vendor data
    if (tud_vendor_mounted()) {
      handle_vendor_out();
    }
  }
}

// Minimal TinyUSB callbacks.  We reset our session when the device
// unmounts (host disconnects).  Stub out other callbacks to avoid
// link errors.
void tud_mount_cb(void) {}
void tud_umount_cb(void) { reset_session(); }
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }
void tud_resume_cb(void) {}