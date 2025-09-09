# Pico2 WebUSB Bulk → SPI Flash Example

This repository is ready to push to GitHub and includes everything you need:

- **Firmware** (`pico2_webusb_example`): C firmware using the pico‑sdk and TinyUSB that exposes a vendor bulk endpoint and writes incoming data to an attached SPI NOR flash.
- **Web App** (`pico2_webusb_example/web`): A simple WebUSB uploader that streams a selected file to the device; includes `.nojekyll` to prevent Jekyll processing when hosted on GitHub Pages.
- **Continuous Integration** (`.github/workflows`): GitHub Actions workflows to build the firmware on every push and to deploy the web app to GitHub Pages.

## Getting Started

1. **Unpack** this tarball/zip into a fresh directory.
2. **Initialize** a new git repository and make your first commit:

   ```bash
   git init
   git add .
   git commit -m "Initial import: Pico2 WebUSB uploader"
   git branch -M main
   git remote add origin <YOUR_GITHUB_REMOTE_URL>
   git push -u origin main
   ```

3. **Configure GitHub Pages**: After pushing, navigate to your repository settings on GitHub. Under **Pages**, ensure the source is set to **GitHub Actions**. The `deploy-pages` workflow will publish `pico2_webusb_example/web` automatically.

## Local Building (Optional)

If you wish to build the firmware locally, install the Pico SDK and Arm GCC toolchain. Then run:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S pico2_webusb_example -B build -DPICO_BOARD=pico2
cmake --build build -j
```

## Adjusting Pins or Flash

The default firmware uses SPI0 on GPIO 2/3/4 with CS on GPIO 5 and assumes a 4 KiB sector erase (e.g., W25Q64). If your hardware differs, edit `src/main.c` and update `PIN_SPI_*`, `PIN_FLASH_CS`, and flash command definitions.

