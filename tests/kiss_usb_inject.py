#!/usr/bin/env python3
"""
KISS USB packet injector using raw fd (no DTR/RTS reset).
Sends a Reticulum packet to Board A via KISS, monitors Board B for relay.

Usage: python tests/kiss_usb_inject.py [message]
"""
import os
import sys
import time
import select
import termios
import threading

# --- Ports ---
KISS_PORT = "/dev/ttyACM1"   # Board A: KISS_OVER_USB
DEBUG_PORT = "/dev/ttyACM0"  # Board B: standard firmware debug

# --- KISS constants ---
FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD

# PLAIN destination hash for ["esp32", "node"]
DEST_HASH = bytes([
    0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E,
    0x01, 0x88, 0x3B, 0xD6, 0x06, 0xC5, 0x42, 0xD7
])


def raw_open(path):
    """Open a serial port without triggering DTR/RTS (no reset)."""
    fd = os.open(path, os.O_RDWR | os.O_NONBLOCK | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    # Raw mode
    attrs[0] = 0        # iflag
    attrs[1] = 0        # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0        # lflag
    attrs[4] = termios.B115200  # ispeed
    attrs[5] = termios.B115200  # ospeed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def kiss_escape(data: bytes) -> bytes:
    out = bytearray()
    for b in data:
        if b == FEND:
            out.extend([FESC, TFEND])
        elif b == FESC:
            out.extend([FESC, TFESC])
        else:
            out.append(b)
    return bytes(out)


def kiss_frame(payload: bytes) -> bytes:
    return bytes([FEND, 0x00]) + kiss_escape(payload) + bytes([FEND])


def build_reticulum_packet(message: str) -> bytes:
    """Build Reticulum wire-format packet: FLAGS HOPS DEST_HASH[16] CONTEXT DATA."""
    flags = 0x08  # PLAIN/BROADCAST/DATA: (0x02 << 2) = 0x08
    hops = 0
    context = 0
    payload = message.encode("utf-8")
    pkt = bytearray()
    pkt.append(flags)
    pkt.append(hops)
    pkt.extend(DEST_HASH)
    pkt.append(context)
    pkt.extend(payload)
    return bytes(pkt)


def monitor_debug(port, stop_event, results):
    """Monitor Board B debug output via raw fd."""
    try:
        fd = raw_open(port)
    except OSError as e:
        print(f"[Monitor] Cannot open {port}: {e}")
        return

    buf = b""
    print(f"[Monitor] Listening on {port}...")
    try:
        while not stop_event.is_set():
            r, _, _ = select.select([fd], [], [], 0.5)
            if r:
                try:
                    chunk = os.read(fd, 4096)
                    if chunk:
                        buf += chunk
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            text = line.decode("utf-8", errors="replace").strip()
                            if text:
                                print(f"  [B] {text}")
                                keywords = [
                                    "ESP-NOW RX", "App Layer Received",
                                    "KISS", "Received packet", "PLAIN",
                                    "Deser", "dest match", "Hello",
                                ]
                                if any(kw in text for kw in keywords):
                                    results.append(text)
                except OSError:
                    pass
    finally:
        os.close(fd)


def main():
    message = "Hello via KISS USB"
    if len(sys.argv) > 1:
        message = " ".join(sys.argv[1:])

    raw_pkt = build_reticulum_packet(message)
    frame = kiss_frame(raw_pkt)

    print("=" * 60)
    print("  KISS USB Packet Injection Test (raw fd, no reset)")
    print("  PC → USB KISS → Board A → ESP-NOW → Board B")
    print("=" * 60)
    print(f"[Pkt] Message: \"{message}\"")
    print(f"[Pkt] Raw: {len(raw_pkt)} bytes, KISS frame: {len(frame)} bytes")
    print(f"[Pkt] Hex: {raw_pkt.hex()}")
    print()

    # Start Board B monitor
    stop_event = threading.Event()
    results = []
    mon = threading.Thread(target=monitor_debug, args=(DEBUG_PORT, stop_event, results), daemon=True)
    mon.start()
    time.sleep(1)

    # Open Board A KISS port (raw, no DTR/RTS)
    try:
        fd = raw_open(KISS_PORT)
        print(f"[KISS] Opened {KISS_PORT} (raw fd, no DTR/RTS)")
    except OSError as e:
        print(f"[KISS] Cannot open {KISS_PORT}: {e}")
        stop_event.set()
        sys.exit(1)

    # Send KISS frames
    for i in range(3):
        try:
            n = os.write(fd, frame)
            print(f"[KISS] Sent frame #{i+1} ({n} bytes)")
        except OSError as e:
            print(f"[KISS] Write error #{i+1}: {e}")
        time.sleep(1.5)

    os.close(fd)
    print(f"[KISS] Closed {KISS_PORT}")

    # Wait for relay evidence
    print()
    print("[Wait] Monitoring Board B for 10s...")
    deadline = time.time() + 10
    while time.time() < deadline:
        if results:
            break
        time.sleep(0.5)

    stop_event.set()
    mon.join(timeout=3)

    print()
    print("=" * 60)
    if results:
        print("  SUCCESS - Packet relayed wirelessly!")
        for r in results:
            print(f"    -> {r}")
    else:
        print("  No relay evidence detected on Board B.")
        print("  Possible causes:")
        print("  - Board A may need time to boot (wait 15s after flash)")
        print("  - Board A KISS processor may not be running")
        print("  - ESP-NOW may not be initialized on Board A")
    print("=" * 60)


if __name__ == "__main__":
    main()
