# Pico2 WebUSB → SPI Flash Uploader

This repository contains a minimal example of how to stream a binary
file from a web page into a Raspberry Pi Pico 2 (RP2350) and write
it to an attached SPI flash chip.  The example consists of two parts:

1. **Firmware** written in C using the official [pico‑sdk][1] and
   [TinyUSB][2].  It exposes a vendor‑specific USB interface with a
   single bulk OUT endpoint (for data) and a bulk IN endpoint (for
   acknowledgements).  When the host sends a header `"FWUP"` followed
   by a 32‑bit little‑endian total size, the firmware erases the flash
   region covering the payload and page‑programs all subsequent bytes.

2. **Web page** (located in the `web/` directory) that uses the
   [WebUSB API][3] to prompt the user for the device, select a file
   using an `<input type="file">`, and then stream the file to the
   firmware in 4‑KiB chunks.  When complete, it reads an optional
   status response from the device and prints messages to a log area.

## Building the firmware

To build the firmware you must have the Pico SDK installed and
`PICO_SDK_PATH` set in your environment.  Then run:

```bash
mkdir build && cd build
cmake .. -DPICO_BOARD=pico2
make -j
```

This produces a `pico2_webusb_spi_uploader.uf2` file in the `build/`
directory.  Boot your Pico 2 into UF2 bootloader mode (hold
`BOOTSEL` while plugging in USB) and copy the UF2 file to the mass
storage device labeled `RPI-RP2`.

## Using the web uploader

Open `web/index.html` in a browser that supports WebUSB (e.g. Chrome
or Edge).  Click **Connect to Device** and select your Pico
device (it will appear with the name provided in the descriptors).
Then choose a file with the file picker.  Progress is displayed
while the file streams.  When complete, the device sends back `"OK"`.

## Notes

- The SPI pins and flash commands are configured for a generic SPI
  NOR flash with 4‑KiB sectors and 256‑byte pages.  Adjust
  `PIN_SPI_*`, `PIN_FLASH_CS`, `FLASH_BAUD`, and the command
  definitions in `src/main.c` for your hardware.
- The included BOS descriptor advertises WebUSB and Microsoft OS 2.0
  capabilities but does not implement vendor request handling or
  provide a landing page URL.  See the TinyUSB examples for
  full WebUSB support.
- For Windows driverless operation you may need to implement the
  Microsoft OS 2.0 descriptor set or supply a WinUSB `.inf` file.

[1]: https://github.com/raspberrypi/pico-sdk
[2]: https://github.com/hathach/tinyusb
[3]: https://wicg.github.io/webusb/