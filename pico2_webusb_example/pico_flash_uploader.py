#!/usr/bin/env python3
"""
pico_flash_uploader.py – Upload a binary image to a Pico 2 device over USB.

This script speaks the simple “FWUP” protocol implemented by the TinyUSB
firmware from the earlier example.  It finds the Pico 2 by its USB vendor
and product identifiers, detaches any kernel‑claimed drivers, and then
streams the selected file over a vendor‑class bulk OUT endpoint.  The
device acknowledges major steps by sending short ASCII status strings on
its bulk IN endpoint (for example "OK" at the end of a successful
transfer).  The script prints these messages so you can watch the
progress.

Usage:

    python3 pico_flash_uploader.py firmware.bin

Common options are provided to adjust the vendor/product IDs, endpoint
addresses, chunk size and timeout.  Run with ``-h`` to see the full
argument list.

Note that the device must be running the companion firmware that
implements the "FWUP" upload protocol.  On the host side, this tool
requires the PyUSB package (install via ``pip install pyusb``).  When
communicating on Linux, you may need to create a udev rule or run the
script with elevated privileges so that libusb can claim the device.

"""

import argparse
import struct
import sys
import time
from typing import Optional

try:
    import usb.core
    import usb.util
except ImportError as exc:
    sys.stderr.write(
        "PyUSB is required for this script.  Install it with 'pip install pyusb'.\n"
    )
    raise


def find_device(vid: int, pid: int) -> usb.core.Device:
    """Locate and return the Pico device with the given vendor and product ID.

    Raises ValueError if the device cannot be found.
    """
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        raise ValueError(
            f"Device not found (VID=0x{vid:04x}, PID=0x{pid:04x}). Is the Pico connected and running the correct firmware?"
        )
    return dev


def detach_kernel_drivers(dev: usb.core.Device) -> None:
    """Detach any active kernel drivers for all interfaces on the device.

    On Linux, the kernel may automatically bind HID or CDC drivers to new
    USB devices.  To allow libusb/pyusb to claim the interface, we must
    detach these drivers.  On other platforms, this function simply
    returns.
    """
    try:
        for configuration in dev:
            for intf in configuration:
                if dev.is_kernel_driver_active(intf.bInterfaceNumber):
                    dev.detach_kernel_driver(intf.bInterfaceNumber)
    except NotImplementedError:
        # Some backends (e.g. Windows) do not implement this check.
        pass


def get_endpoints(
    dev: usb.core.Device, interface_index: int, out_ep_addr: int, in_ep_addr: int
) -> tuple[usb.core.Endpoint, usb.core.Endpoint]:
    """Return the OUT and IN endpoints from the specified interface.

    The Pico firmware exposes a single vendor interface (interface 0,
    alternate setting 0) with two bulk endpoints: one for OUT transfers
    (host‑to‑device) and one for IN transfers (device‑to‑host).  This
    helper locates those endpoints by address.
    """
    cfg = dev.get_active_configuration()
    # Interfaces are indexed by (interface number, alternate setting).
    intf = cfg[(interface_index, 0)]
    out_ep = usb.util.find_descriptor(
        intf, custom_match=lambda e: e.bEndpointAddress == out_ep_addr
    )
    in_ep = usb.util.find_descriptor(
        intf, custom_match=lambda e: e.bEndpointAddress == in_ep_addr
    )
    if out_ep is None or in_ep is None:
        raise ValueError(
            f"Could not find both endpoints on interface {interface_index}.\n"
            f"Found OUT: {out_ep is not None}, IN: {in_ep is not None}"
        )
    return out_ep, in_ep

def read_status(
    in_ep: usb.core.Endpoint, timeout: int, suppress_exceptions: bool = True
) -> Optional[str]:
    """Attempt to read a status message from the device.

    If no data is available within the specified timeout, returns None.  If
    data is received, decodes it as UTF‑8 (ignoring decode errors) and
    returns the resulting string.  If ``suppress_exceptions`` is False,
    propagates USB errors instead of returning None.
    """
    try:
        # wMaxPacketSize gives the maximum size we can read in one go
        data = in_ep.read(in_ep.wMaxPacketSize, timeout=timeout)
        if not data:
            return None
        # Convert to bytes then decode
        msg = bytes(data).decode("utf-8", errors="ignore").strip()
        return msg or None
    except usb.core.USBError as exc:
            # Timeout errors are common if the device hasn't sent anything.
        if suppress_exceptions and exc.errno is None:
            return None
        if suppress_exceptions and exc.errno in [110, 60]:  # Operation timed out
            return None
        # Unexpected error: re‑raise
        raise

def end_index(buffer: str, target: str) -> int:
    if target not in buffer:
        return None
    target_rindex = buffer.rindex(target)

    return (target_rindex, target_rindex + len(target))

def wait_for_status(status_buffer: str, target_string: str, in_ep: usb.core.Endpoint, timeout: int, delay: float = 0.1) -> str:
    print(f'-------------------------- Checking for {target_string} -------------------------- ')
    
    while target_string not in status_buffer:
        status = read_status(in_ep, timeout)
        if status:
            status_buffer += status
            continue
        else:
            time.sleep(delay)
    print(f"SB:{status_buffer}\n")
    target_slice_start, target_slice_end = end_index(status_buffer, target_string)
    status_buffer = status_buffer[target_slice_end:]
    print(f"SB:{status_buffer}\n")

    print(f'-------------------------- {target_string} → NEXT? -------------------------- ')

def upload_file(
    dev: usb.core.Device,
    filename: str,
    out_ep: usb.core.Endpoint,
    in_ep: usb.core.Endpoint,
    chunk_size: int = 4096,
    timeout: int = 10000,
    wait_after_header: float = 0.0,
) -> None:
    """Stream the contents of ``filename`` to the Pico via bulk endpoint.

    This sends an 8‑byte header ('FWUP' + little‑endian length), then
    transmits the file in chunks.  After each write, it attempts to read
    and print a status string from the device.  At the end of the
    transfer, it waits for a final status message (typically "OK") to
    confirm that the firmware finished writing the flash.
    """

    status = ""
    status_buffer = ""
    # Read file into memory.  For very large images, you may prefer
    # streaming from disk, but reading all data simplifies the logic.
    with open(filename, "rb") as f:
        data = f.read()
    total_len = len(data)

    print(f"Uploading {filename} ({total_len} bytes)")

    # Construct and send header: magic + length
    header = b"FWUP" + struct.pack("<I", total_len)
    print("→ Sending header…", end=" ")
    out_ep.write(header, timeout)
    print("done")

    # If the device performs a lengthy erase of its flash after
    # receiving the header, the host must wait before sending the
    # payload.  Use the ``wait_after_header`` parameter to add a
    # configurable delay here.  Without this pause, large uploads may
    # cause the host to time out because the device is not ready to
    # accept data while erasing.
    if wait_after_header > 0:
        print(f"→ Waiting {wait_after_header:.1f}s for device to prepare…")
        time.sleep(wait_after_header)

    # once waiting for the erase works we can refactor this to use the wait_for_status function
    print('-------------------------- Checking for HEADER_OK -------------------------- ')


    target_string = f"HEADER_OK {total_len}"
    
    while target_string not in status_buffer:
        status = read_status(in_ep, timeout)
        if status:
            status_buffer += status
            continue
        else:
            sleep(0.1)
    target_slice_start, target_slice_end = end_index(status_buffer, target_string)
    status_buffer = status_buffer[target_slice_end:]
    print(f"SB:{status_buffer}\n")

    print('-------------------------- HEADER_OK → ERASE_START -------------------------- ')

    # Read any immediate status (e.g. device acknowledging erase)
    print("→ Reading status…", end=" ")

    wait_for_status(status_buffer, "ERASE_START", in_ep, timeout)
    print("Erase Started...")
    wait_for_status(status_buffer, "ERASE_DONE", in_ep, timeout)
    print("Erase Done...")



    # Send data in chunks
    bytes_sent = 0
    # Simple progress indicator without external dependencies
    last_percent = -1
    while bytes_sent < total_len:
        chunk = data[bytes_sent : bytes_sent + chunk_size]
        bytes_sent += out_ep.write(chunk, timeout)
        # Progress output
        # percent = int(bytes_sent * 100 / total_len)
        # if percent != last_percent:
        #     sys.stdout.write(f"\r→ Progress: {percent}%")
        #     sys.stdout.flush()
        #     last_percent = percent
        # Read intermediate status messages, if any
        try:
            status = read_status(in_ep, 5, suppress_exceptions=True)
        except usb.core.USBError as e:
            pass
        if status:
            print(f"{status}") # Called 3 times

    print("")

    # Wait for final status
    print("→ Waiting for completion acknowledgement…", end=" ")
    done_msg = None
    # Loop until we read a message or a timeout occurs
    start_time = time.monotonic()
    while True:
        done_msg = read_status(in_ep, timeout, suppress_exceptions=True)
        if done_msg:
            break
        if time.monotonic() - start_time > (timeout / 1000.0):
            # If the device does not respond in time, give up
            break
    if done_msg:
        print("done")
        print(f"Device: {done_msg}")
    else:
        print("no acknowledgement received (timeout)")


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Upload a file to a Pico 2 over USB bulk transfer."
    )
    parser.add_argument(
        "file",
        help="Path to the binary image or any file to upload",
    )
    parser.add_argument(
        "--vid",
        type=lambda x: int(x, 0),
        default=0xCAFE,
        help="Vendor ID of the device (hex or decimal, default: 0xCAFE)",
    )
    parser.add_argument(
        "--pid",
        type=lambda x: int(x, 0),
        default=0x4001,
        help="Product ID of the device (hex or decimal, default: 0x4001)",
    )
    parser.add_argument(
        "--out-ep",
        type=lambda x: int(x, 0),
        default=0x01,
        help="Address of the bulk OUT endpoint (default: 0x01)",
    )
    parser.add_argument(
        "--in-ep",
        type=lambda x: int(x, 0),
        default=0x81,
        help="Address of the bulk IN endpoint (default: 0x81)",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=4096,
        help="Number of bytes to send per USB packet (default: 4096)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=10000,
        help="Timeout in milliseconds for USB operations (default: 10000)",
    )
    parser.add_argument(
        "--wait-after-header",
        type=float,
        default=0.0,
        help=(
            "Seconds to wait after sending the header before sending data. "
            "Increase this for large images if the device erases flash after receiving the header"
        ),
    )
    args = parser.parse_args(argv)

    # Locate device
    try:
        dev = find_device(args.vid, args.pid)
    except ValueError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    # Detach any kernel driver and set configuration
    detach_kernel_drivers(dev)
    # Ensure a configuration is active.  Many devices have only one.
    dev.set_configuration()

    # Get endpoints from interface 0.  You can adjust if your descriptor
    # changes.
    try:
        out_ep, in_ep = get_endpoints(dev, 0, args.out_ep, args.in_ep)
    except ValueError as e:
        sys.stderr.write(str(e) + "\n")
        return 1

    try:
        upload_file(
            dev,
            args.file,
            out_ep,
            in_ep,
            chunk_size=args.chunk_size,
            timeout=args.timeout,
            wait_after_header=args.wait_after_header,
        )
    except usb.core.USBError as e:
        sys.stderr.write(f"---------------USB error: {e}-----------------------\n")
        raise e
        return 1
    except KeyboardInterrupt:
        sys.stderr.write("\nUpload interrupted by user.\n")
        return 1
    return 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())