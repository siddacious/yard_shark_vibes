cmake_minimum_required(VERSION 3.13)

# Import the Pico SDK CMake helpers.  This assumes you have
# `PICO_SDK_PATH` exported in your environment.  See the Pico SDK
# getting‑started guide for details on how to set this up.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Name the overall project and specify language types.  The project
# includes C, C++ and assembly sources but we only need C for this
# example.  Use C11 and C++17 by default.
project(pico2_webusb_spi_uploader C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialise the Pico SDK.  This provides the include paths, build
# system rules and linker scripts required for the RP2040/RP2350.
pico_sdk_init()

# Define the executable and list all source files.  If you add more
# source files, update this list accordingly.  The dummy PIO source
# generates a header even though it's unused; some toolchains expect
# at least one PIO file when using pico_generate_pio_header().
add_executable(pico2_webusb_spi_uploader
    src/main.c
    src/usb_descriptors.c
)

# Generate the PIO header to satisfy build rules.  The empty .pio
# file is only used so that the build system does not warn about
# missing PIO sources.
pico_generate_pio_header(pico2_webusb_spi_uploader ${CMAKE_CURRENT_LIST_DIR}/src/dummy.pio)

# Add this directory to the include search path for this target.  All
# header files in src/ are therefore found without needing to specify
# relative paths in the source code.
target_include_directories(pico2_webusb_spi_uploader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Link against Pico SDK libraries.  In addition to the standard
# library (`pico_stdlib`), we link the hardware SPI and sync
# libraries and TinyUSB for device support.  The TinyUSB device
# implementation is provided by `tinyusb_device` and the board‑
# specific glue by `tinyusb_board`.
target_link_libraries(pico2_webusb_spi_uploader
    pico_stdlib
    hardware_spi
    hardware_sync
    tinyusb_device
    tinyusb_board
)

# Enable USB for standard I/O and disable UART.  This makes the
# device present itself as a USB CDC serial port for debugging (if
# needed) and avoids a second UART printing to pins 0/1.
pico_enable_stdio_usb(pico2_webusb_spi_uploader 1)
pico_enable_stdio_uart(pico2_webusb_spi_uploader 0)

# Produce additional output formats (binary and UF2) for convenience.
pico_add_extra_outputs(pico2_webusb_spi_uploader)