"""Helper script to send debug commands via KISS over serial.

Usage: adjust PORT to your device COM port and run the script to ask the
node to print its routing table or ESP-NOW peer list. The node must have
loaded firmware with ESP-NOW support.

Example:
    python tests/espnow_debug.py COM16 routes

The command is sent as a LOCAL_CMD packet (context 0xB0) with the first 8
bytes reserved for a destination address. We use all-zero address so the
node treats the payload as a local request.
"""

import sys
import serial
import time

FEND = 0xC0
CMD_DATA = 0x00
LOCAL_CMD = 0xB0


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


def main():
    if len(sys.argv) < 3:
        print("Usage: espnow_debug.py <COM port> <command>")
        print("Commands: routes, peers")
        return
    port = sys.argv[1]
    cmd = sys.argv[2].encode('ascii')

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        print(f"Failed to open {port}: {e}")
        return

    # Reticulum Header Type 1: flags, hops, 16-byte destination, context, data.
    # LOCAL_CMD is consumed only on this local KISS interface, so the header
    # destination can be all zero; the command target prefix is also zero.
    packet = bytes([0x00, 0x00]) + bytes(16) + bytes([LOCAL_CMD]) + bytes(8) + cmd
    frame = make_kiss_frame(packet)
    ser.write(frame)
    print(f"Sent command '{cmd.decode()}' to device on {port}")
    # read any immediate replies for a short while
    start = time.time()
    while time.time() - start < 3:
        data = ser.readline()
        if data:
            print(data.decode('utf-8', errors='ignore').rstrip())
    ser.close()


if __name__ == '__main__':
    main()
