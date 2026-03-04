#!/usr/bin/env python3
"""
Simple KISS round-trip tester for two boards.

This script opens two serial ports (defaults to COM16 and COM22) and
sends a valid Reticulum-formatted packet out of each one.  It also
listens on both ports and decodes any KISS frames it sees.  The
purpose is to demonstrate the framing, not to magically make the
boards reply; you'll need either:

  * the "KISS over USB" firmware build (see Config.h) so that the
    CDC device on COM16/COM22 is actually the KISS link, or
  * a USB‑UART adapter wired to GPIO2/4 (or whatever pins your board
    is using) so that the proper Serial1 interface appears as a COM port.

With a responding board the script will print the received payload
hex.  Without a response you'll only see the "sent" messages.  This
is normal if the other end is not echoing anything.

Usage:
    python kiss_roundtrip.py [port1] [port2]

Example:
    python kiss_roundtrip.py COM16 COM22

Note: the script imports a small portion of the Reticulum library only
for packet construction.  It doesn't attempt to run a full Reticulum
node and therefore uses manual framing.
"""

import sys
import time
import threading

# imports from the installed Reticulum library
try:
    import RNS
    from RNS.Packet import Packet
    from RNS.Destination import Destination
except ImportError:
    print("This script requires the Reticulum Python library (pip install reticulum)")
    sys.exit(1)

import serial

# constants for KISS
FEND = 0xC0
CMD_DATA = 0x00


def kiss_escape(data: bytes) -> bytes:
    out = bytearray()
    for b in data:
        if b == FEND:
            out += bytes([0xDB, 0xDC])
        elif b == 0xDB:
            out += bytes([0xDB, 0xDD])
        else:
            out.append(b)
    return bytes(out)


def make_kiss_frame(payload: bytes) -> bytes:
    frame = bytearray()
    frame.append(FEND)
    frame.append(CMD_DATA)
    frame += kiss_escape(payload)
    frame.append(FEND)
    return bytes(frame)


def readable_loop(ser, name):
    buf = bytearray()
    in_frame = False
    while True:
        b = ser.read(1)
        if not b:
            time.sleep(0.05)
            continue
        byte = b[0]
        if byte == FEND:
            if in_frame and buf:
                print(f"[{name}] received {len(buf)} bytes payload: {buf.hex()}")
                buf.clear()
            in_frame = True
            continue
        if in_frame:
            if byte == 0xDB:
                nxt = ser.read(1)
                if nxt and nxt[0] == 0xDC:
                    buf.append(FEND)
                elif nxt and nxt[0] == 0xDD:
                    buf.append(0xDB)
                continue
            buf.append(byte)


def open_port(port):
    for i in range(5):
        try:
            return serial.Serial(port, 115200, timeout=1)
        except serial.SerialException as e:
            print(f"{port}: {e}, retrying...")
            time.sleep(0.5)
    raise IOError(f"failed to open {port}")


def build_reticulum_packet(dest_hash: bytes, data: bytes) -> bytes:
    """Use the (installed) Reticulum library to construct a raw packet."""
    # create a Destination object with the given hash
    class TempDest:
        def __init__(self, h):
            self.hash = h
            self.type = RNS.Destination.SINGLE
        def encrypt(self, data):
            # local command packets aren't encrypted anyway
            return data
    dest = TempDest(dest_hash)
    pkt = Packet(dest, data, packet_type=Packet.DATA, context=Packet.NONE)
    pkt.pack()
    return pkt.raw


def main():
    if len(sys.argv) >= 3:
        ports = [sys.argv[1], sys.argv[2]]
    else:
        ports = ["COM16", "COM22"]
    serial_objs = {}
    for p in ports:
        try:
            serial_objs[p] = open_port(p)
        except Exception as e:
            print(f"cannot open {p}: {e}")
            return
        print(f"opened {p}")
    # spawn readers
    for p, ser in serial_objs.items():
        t = threading.Thread(target=readable_loop, args=(ser, p), daemon=True)
        t.start()
    time.sleep(1)

    # build a test packet (destination arbitrary)
    # use 16 zero bytes which is a broadcast/plain hash (might be ignored)
    dest_hash = bytes([0] * 16)
    payload = b"roundtrip test"
    raw = build_reticulum_packet(dest_hash, payload)
    print(f"constructed {len(raw)}-byte RNS packet: {raw.hex()}")

    # send one frame on each port
    for p in ports:
        ser = serial_objs[p]
        frame = make_kiss_frame(raw)
        ser.write(frame)
        ser.flush()
        print(f"[{p}] sent {len(frame)}-byte KISS frame")

    # let readers run a bit
    time.sleep(5)

if __name__ == "__main__":
    main()
