#!/usr/bin/env python3
"""
Real KISS Mesh Relay Test
=========================
Sends a real Reticulum packet from PC → KISS USB → Board A → ESP-NOW → Board B

Board A (/dev/ttyACM0): esp32-c3-kiss-usb firmware (KISS TNC over USB CDC)
Board B (/dev/ttyACM1): esp32-c3-devkitm-1 firmware (debug output over USB CDC)

This script:
  1. Constructs a valid Reticulum wire-format packet targeting the PLAIN
     destination ["esp32", "node"] (hash B6010EA11FDFC04E01883BD606C542D7)
  2. Wraps it in KISS framing (FEND + 0x00 cmd + escaped data + FEND)
  3. Sends it to Board A via /dev/ttyACM0
  4. Monitors Board B's debug output on /dev/ttyACM1 for the packet arriving
     via ESP-NOW

No simulation — real packets over real wireless.
"""

import sys
import time
import struct
import threading
import serial

# --- Configuration ---
KISS_PORT = "/dev/ttyACM0"   # Board A: KISS TNC (esp32-c3-kiss-usb)
DEBUG_PORT = "/dev/ttyACM1"  # Board B: Debug serial (esp32-c3-devkitm-1)
BAUD = 115200
TIMEOUT = 15  # seconds to wait for relay

# --- KISS framing constants ---
FEND  = 0xC0
FESC  = 0xDB
TFEND = 0xDC
TFESC = 0xDD

# --- Reticulum wire format constants ---
# From the firmware's ReticulumPacket.h / Config.h
HEADER_TYPE_1 = 0  # 1-byte header type (no transport)
PACKET_DATA   = 0  # Data packet
DEST_PLAIN    = 0  # PLAIN destination type
PROP_BROADCAST = 0 # Broadcast propagation
CONTEXT_NONE  = 0  # No context

# PLAIN destination hash for ["esp32", "node"]
DEST_HASH = bytes([
    0xB6, 0x01, 0x0E, 0xA1, 0x1F, 0xDF, 0xC0, 0x4E,
    0x01, 0x88, 0x3B, 0xD6, 0x06, 0xC5, 0x42, 0xD7
])


def kiss_escape(data: bytes) -> bytes:
    """KISS-escape special bytes in the data."""
    out = bytearray()
    for b in data:
        if b == FEND:
            out.append(FESC)
            out.append(TFEND)
        elif b == FESC:
            out.append(FESC)
            out.append(TFESC)
        else:
            out.append(b)
    return bytes(out)


def kiss_frame(payload: bytes) -> bytes:
    """Wrap a raw packet in a KISS frame: FEND + cmd(0x00) + escaped_data + FEND."""
    return bytes([FEND, 0x00]) + kiss_escape(payload) + bytes([FEND])


def build_reticulum_packet(message: str) -> bytes:
    """
    Build a raw Reticulum wire-format packet matching the firmware's format.

    Firmware wire format (ReticulumPacket.cpp):
      [FLAGS 1] [HOPS 1] [DEST_HASH 16] [CONTEXT 1] [DATA]

    FLAGS byte layout (from serialize()):
      bits [1:0] = packet_type   (DATA=0x00)
      bits [3:2] = dest_type     (PLAIN=0x02)
      bit  [4]   = propagation   (BROADCAST=0x00)
      bit  [5]   = context_flag  (0)
      bit  [6]   = header_type   (HEADER_1=0)
      bit  [7]   = ifac_flag     (0)

    For PLAIN/BROADCAST/DATA: flags = (0x02 << 2) = 0x08
    """
    packet_type = 0x00    # RNS_PACKET_DATA
    dest_type = 0x02      # RNS_DEST_PLAIN
    propagation = 0x00    # RNS_PROPAGATION_BROADCAST

    flags = (
        (packet_type & 0x03) |
        ((dest_type & 0x03) << 2) |
        ((propagation & 0x01) << 4) |
        (0 << 5) |   # context_flag
        (0 << 6) |   # header_type = HEADER_1
        (0 << 7)     # ifac_flag = 0
    )

    hops = 0
    context = CONTEXT_NONE
    payload = message.encode("utf-8")

    packet = bytearray()
    packet.append(flags)           # FLAGS byte (0x08 for PLAIN/BROADCAST/DATA)
    packet.append(hops)            # HOPS
    packet.extend(DEST_HASH[:16])  # 16-byte destination hash
    packet.append(context)         # CONTEXT byte (at offset 18)
    packet.extend(payload)         # DATA payload

    return bytes(packet)


def monitor_board_b(stop_event: threading.Event, results: list):
    """Monitor Board B debug serial for incoming packet evidence."""
    try:
        ser = serial.Serial(DEBUG_PORT, BAUD, timeout=1)
        print(f"[Monitor] Listening on {DEBUG_PORT} for Board B debug output...")
        while not stop_event.is_set():
            line = ser.readline()
            if line:
                text = line.decode("utf-8", errors="replace").strip()
                if text:
                    print(f"  [B] {text}")
                    # Look for signs the packet was received
                    if any(kw in text for kw in [
                        "ESP-NOW RX",
                        "App Layer Received",
                        "KISS mesh test",
                        "Received packet",
                        "PLAIN",
                        "from PC",
                        "Deser",
                        "dest match",
                    ]):
                        results.append(text)
        ser.close()
    except serial.SerialException as e:
        print(f"[Monitor] Error on {DEBUG_PORT}: {e}")


def main():
    message = "KISS mesh test from PC"
    if len(sys.argv) > 1:
        message = " ".join(sys.argv[1:])

    print("=" * 60)
    print("  Real KISS Mesh Relay Test")
    print("  PC → USB KISS → Board A → ESP-NOW → Board B")
    print("=" * 60)
    print()

    # Build packet
    raw_packet = build_reticulum_packet(message)
    frame = kiss_frame(raw_packet)

    print(f"[Packet] Message: \"{message}\"")
    print(f"[Packet] Dest hash: {DEST_HASH.hex()}")
    print(f"[Packet] Raw size: {len(raw_packet)} bytes")
    print(f"[Packet] KISS frame size: {len(frame)} bytes")
    print(f"[Packet] Raw hex: {raw_packet.hex()}")
    print()

    # Start monitoring Board B in background
    stop_event = threading.Event()
    results = []
    monitor_thread = threading.Thread(
        target=monitor_board_b, args=(stop_event, results), daemon=True
    )
    monitor_thread.start()
    time.sleep(1)  # Let monitor settle

    # Open KISS interface to Board A and send
    try:
        # Open without toggling DTR/RTS to avoid resetting the ESP32
        kiss_ser = serial.Serial()
        kiss_ser.port = KISS_PORT
        kiss_ser.baudrate = BAUD
        kiss_ser.timeout = 2
        kiss_ser.dtr = False
        kiss_ser.rts = False
        kiss_ser.open()
        time.sleep(3)  # ESP32-C3 USB CDC needs time to settle after port open
        # Drain any stale data
        kiss_ser.reset_input_buffer()
        print(f"[KISS] Opened {KISS_PORT} (DTR/RTS disabled)")
        print(f"[KISS] Sending KISS frame ({len(frame)} bytes)...")

        # Send multiple times for reliability
        for i in range(3):
            try:
                kiss_ser.write(frame)
                # flush can fail on ESP32-C3 USB CDC — not fatal
                try:
                    kiss_ser.flush()
                except (serial.SerialException, OSError):
                    print(f"[KISS] flush() failed (USB CDC quirk), continuing...")
                    time.sleep(0.5)
                print(f"[KISS] Sent packet #{i+1}")
            except (serial.SerialException, OSError) as e:
                print(f"[KISS] Write error on packet #{i+1}: {e}")
                # Try reopening
                try:
                    kiss_ser.close()
                except Exception:
                    pass
                time.sleep(2)
                try:
                    kiss_ser.open()
                    time.sleep(1)
                    print(f"[KISS] Reopened {KISS_PORT}")
                except Exception as e2:
                    print(f"[KISS] Reopen failed: {e2}")
                    break
            time.sleep(1.5)

        kiss_ser.close()
        print(f"[KISS] Closed {KISS_PORT}")
    except serial.SerialException as e:
        print(f"[KISS] Error: {e}")
        stop_event.set()
        sys.exit(1)

    # Wait for Board B to show evidence of receipt
    print()
    print(f"[Wait] Monitoring Board B for {TIMEOUT}s...")
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        if results:
            break
        time.sleep(0.5)

    stop_event.set()
    monitor_thread.join(timeout=3)

    print()
    print("=" * 60)
    if results:
        print("  SUCCESS — Packet relayed wirelessly!")
        print(f"  Evidence lines: {len(results)}")
        for r in results:
            print(f"    → {r}")
    else:
        print("  No relay evidence detected on Board B.")
        print("  (Check that both boards are powered and ESP-NOW is active)")
    print("=" * 60)


if __name__ == "__main__":
    main()
