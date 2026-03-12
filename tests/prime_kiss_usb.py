#!/usr/bin/env python3
"""Prime an ESP32-C3 USB KISS port without toggling DTR/RTS.

The ESP32-C3 USB Serial/JTAG CDC path may not flush outbound traffic until the
host has sent at least one USB OUT packet. This helper opens the port as a raw
file descriptor and writes an empty KISS data frame, which is a harmless way to
activate the CDC path for the KISS TNC firmware.

Usage:
    python tests/prime_kiss_usb.py /dev/ttyACM0
"""

import os
import sys
import termios

FEND = 0xC0


def raw_open(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python tests/prime_kiss_usb.py /dev/ttyACMx")
        return 1

    port = sys.argv[1]
    frame = bytes([FEND, 0x00, FEND])

    try:
        fd = raw_open(port)
    except OSError as exc:
        print(f"Could not open {port}: {exc}")
        return 1

    try:
        written = os.write(fd, frame)
        print(f"Primed {port} with empty KISS frame ({written} bytes)")
    finally:
        os.close(fd)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())