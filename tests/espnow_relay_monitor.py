#!/usr/bin/env python3
"""
Real ESP-NOW Mesh Relay Monitor
================================
Monitors debug output from two ESP32-C3 boards simultaneously.
Board A sends real Reticulum packets via ESP-NOW to Board B.
Board B receives and displays them.

Uses raw file descriptors to avoid pyserial DTR/RTS issues
that reset ESP32-C3 boards into bootloader mode.

Usage:
    python3 tests/espnow_relay_monitor.py [port_a] [port_b]
"""

import os
import sys
import time
import select
import termios
import fcntl

PORT_A = "/dev/ttyACM0"
PORT_B = "/dev/ttyACM2"  # May shift after USB re-enum; adjust as needed

BAUD = 115200

# Color codes for terminal output
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

HIGHLIGHT_KEYWORDS = [
    ("ESP-NOW RX", CYAN),
    ("App Layer Received", GREEN + BOLD),
    ("KISS", YELLOW),
    ("PLAIN", GREEN),
    ("Deser", CYAN),
    ("dest match", GREEN),
    ("Sending packet", YELLOW + BOLD),
    ("SENDING PACKET", YELLOW + BOLD),
    ("Forwarding", YELLOW),
    ("sendPacket", YELLOW),
    ("Hello from ESP32", GREEN + BOLD),
    ("announce", CYAN),
    ("peer list", ""),
]


def configure_serial(fd, baud=115200):
    """Configure the serial port via termios without touching modem signals."""
    attrs = termios.tcgetattr(fd)
    # Input flags: raw
    attrs[0] = 0  # iflag
    # Output flags: raw
    attrs[1] = 0  # oflag
    # Control flags: baud, 8N1, local, enable receiver
    attrs[2] = (
        termios.CS8
        | termios.CLOCAL
        | termios.CREAD
        | termios.B115200
    )
    # Local flags: raw
    attrs[3] = 0  # lflag
    # Special characters
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1  # 100ms timeout
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def open_port(path):
    """Open serial port without DTR/RTS manipulation."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
        configure_serial(fd)
        return fd
    except OSError as e:
        print(f"  Cannot open {path}: {e}")
        return None


def colorize(text):
    """Highlight important keywords in the output."""
    for keyword, color in HIGHLIGHT_KEYWORDS:
        if keyword.lower() in text.lower():
            if color:
                return color + text + RESET
            break
    return text


def main():
    global PORT_A, PORT_B

    if len(sys.argv) >= 3:
        PORT_A = sys.argv[1]
        PORT_B = sys.argv[2]
    elif len(sys.argv) == 2:
        PORT_A = sys.argv[1]

    # Auto-detect ports
    acm_ports = sorted(
        [f"/dev/{p}" for p in os.listdir("/dev") if p.startswith("ttyACM")]
    )
    if len(acm_ports) >= 2 and len(sys.argv) < 3:
        PORT_A = acm_ports[0]
        PORT_B = acm_ports[1]
    elif len(acm_ports) == 1:
        PORT_A = acm_ports[0]
        PORT_B = None

    print(f"{BOLD}{'=' * 60}{RESET}")
    print(f"{BOLD}  Real ESP-NOW Mesh Relay Monitor{RESET}")
    print(f"{BOLD}  Board A → ESP-NOW wireless → Board B{RESET}")
    print(f"{BOLD}{'=' * 60}{RESET}")
    print()
    print(f"  Board A: {PORT_A}")
    print(f"  Board B: {PORT_B or 'not connected'}")
    print()

    fd_a = open_port(PORT_A) if PORT_A else None
    fd_b = open_port(PORT_B) if PORT_B else None

    if not fd_a and not fd_b:
        print("No boards detected. Exiting.")
        sys.exit(1)

    fds = {}
    if fd_a is not None:
        fds[fd_a] = ("A", RED)
    if fd_b is not None:
        fds[fd_b] = ("B", GREEN)

    buffers = {fd: b"" for fd in fds}
    stats = {"a_lines": 0, "b_lines": 0, "espnow_rx": 0, "packets": 0}

    print(f"  Monitoring... (Ctrl+C to stop)")
    print(f"  {'-' * 56}")

    try:
        while True:
            # Wait for data on any port
            readable, _, _ = select.select(list(fds.keys()), [], [], 1.0)

            for fd in readable:
                label, color = fds[fd]
                try:
                    chunk = os.read(fd, 4096)
                    if chunk:
                        buffers[fd] += chunk
                except BlockingIOError:
                    continue
                except OSError:
                    continue

            # Process line buffers
            for fd in list(fds.keys()):
                while b"\n" in buffers[fd]:
                    line, buffers[fd] = buffers[fd].split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").strip()
                    if not text:
                        continue

                    label, color = fds[fd]
                    if label == "A":
                        stats["a_lines"] += 1
                    else:
                        stats["b_lines"] += 1

                    if "ESP-NOW RX" in text:
                        stats["espnow_rx"] += 1
                    if "App Layer Received" in text or "SENDING PACKET" in text:
                        stats["packets"] += 1

                    colored = colorize(text)
                    print(f"  {color}[{label}]{RESET} {colored}")

    except KeyboardInterrupt:
        print()
        print(f"  {'-' * 56}")
        print(f"  {BOLD}Session Stats:{RESET}")
        print(f"    Board A lines: {stats['a_lines']}")
        print(f"    Board B lines: {stats['b_lines']}")
        print(f"    ESP-NOW RX events: {stats['espnow_rx']}")
        print(f"    Packet events: {stats['packets']}")
        print(f"  {'=' * 60}")
    finally:
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                pass


if __name__ == "__main__":
    main()
