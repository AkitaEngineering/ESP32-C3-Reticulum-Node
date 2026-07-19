#!/usr/bin/env python3
"""
Raw Serial KISS Sniffer - Shows what's actually coming over the serial port
This bypasses Reticulum to see the raw KISS frames
"""
import serial
import sys
import time

# Configure serial port - change this to match your ESP32 Serial2 port
SERIAL_PORT = "/dev/ttyUSB1"  # Adjust to your Serial2 (UART2) port
BAUD_RATE = 115200

# KISS constants
FEND = 0xC0
FESC = 0xDB
TFEND = 0xDC
TFESC = 0xDD

def decode_kiss_frame(data):
    """Decode a KISS frame"""
    if len(data) < 2:
        return None

    # KISS frames start and end with FEND
    if data[0] != FEND or data[-1] != FEND:
        print(f"  WARNING: Frame doesn't start/end with FEND (0xC0)")

    # Remove FENDs and unescape
    unescaped = []
    i = 1  # Skip first FEND
    while i < len(data) - 1:  # Skip last FEND
        if data[i] == FESC:
            if i + 1 < len(data) - 1:
                if data[i+1] == TFEND:
                    unescaped.append(FEND)
                    i += 2
                elif data[i+1] == TFESC:
                    unescaped.append(FESC)
                    i += 2
                else:
                    unescaped.append(data[i])
                    i += 1
            else:
                i += 1
        else:
            unescaped.append(data[i])
            i += 1

    return bytes(unescaped)

def analyze_packet(packet):
    """Analyze Reticulum packet structure (official format)"""
    if len(packet) < 1:
        print(f"  Packet too short")
        return

    print(f"\n  RETICULUM PACKET ANALYSIS:")
    print(f"  -------------------------")

    offset = 0

    # Check if first byte is KISS command byte (0x00)
    if packet[0] == 0x00:
        print(f"  KISS Command: 0x00 (DATA FRAME)")
        offset = 1

    if len(packet) < offset + 19:  # Minimum: FLAGS + HOPS + DEST_HASH(16) + CONTEXT
        print(f"  Packet too short for Reticulum header (need {offset + 19}, got {len(packet)})")
        return

    # Official Reticulum format: [FLAGS 1][HOPS 1][DEST_HASH 16][CONTEXT 1][DATA]
    flags = packet[offset]
    hops = packet[offset + 1]
    dest_hash = packet[offset + 2:offset + 18]
    context = packet[offset + 18]

    # Decode flags byte
    packet_type = flags & 0b11
    dest_type = (flags >> 2) & 0b11
    prop_type = (flags >> 4) & 0b1
    context_flag = (flags >> 5) & 0b1
    header_type = (flags >> 6) & 0b1
    ifac_flag = (flags >> 7) & 0b1

    print(f"  FLAGS: 0x{flags:02X} (0b{flags:08b})")
    print(f"    - Packet Type: {['DATA', 'ANNOUNCE', 'LINKREQ', 'PROOF'][packet_type]}")
    print(f"    - Dest Type: {['SINGLE', 'GROUP', 'PLAIN', 'LINK'][dest_type]}")
    print(f"    - Propagation: {['BROADCAST', 'TRANSPORT'][prop_type]}")
    print(f"    - Context Flag: {context_flag}")
    print(f"    - Header Type: {['HEADER_1', 'HEADER_2'][header_type]}")
    print(f"    - IFAC Flag: {ifac_flag}")

    print(f"  HOPS: {hops}")
    print(f"  DEST_HASH (16 bytes): {dest_hash.hex()}")

    print(f"  CONTEXT: 0x{context:02X}", end="")
    if context == 0x00:
        print(" (NONE)")
    elif context == 0xB0:
        print(" (LOCAL_CMD)")
    elif context == 0xFA:
        print(" (KEEPALIVE)")
    elif context == 0xFC:
        print(" (LINKCLOSE)")
    elif context == 0xFE:
        print(" (LRRTT)")
    elif context == 0xFF:
        print(" (LRPROOF)")
    else:
        print(" (UNKNOWN)")

    # Data payload starts after context
    if len(packet) > offset + 19:
        data = packet[offset + 19:]
        print(f"  DATA ({len(data)} bytes): {data.hex()}")
        try:
            text = data.decode('utf-8', errors='replace')
            if text.isprintable():
                print(f"  DATA (text): '{text}'")
        except:
            pass

print("=" * 70)
print("RAW SERIAL KISS SNIFFER")
print("=" * 70)
print(f"Port: {SERIAL_PORT}")
print(f"Baud: {BAUD_RATE}")
print(f"Press Ctrl+C to exit\n")

try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"Serial port opened successfully\n")
    print("Waiting for KISS frames...\n")

    buffer = bytearray()
    frame_count = 0

    while True:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)

            # Look for complete KISS frames (start with FEND, end with FEND)
            while True:
                # Find first FEND
                start = buffer.find(FEND)
                if start == -1:
                    buffer.clear()
                    break

                # Find next FEND (end of frame)
                end = buffer.find(FEND, start + 1)
                if end == -1:
                    # Incomplete frame, keep buffer and wait for more data
                    buffer = buffer[start:]
                    break

                # Extract frame
                frame = bytes(buffer[start:end+1])
                buffer = buffer[end+1:]

                frame_count += 1
                print("=" * 70)
                print(f"KISS FRAME #{frame_count} at {time.strftime('%H:%M:%S')}")
                print("=" * 70)
                print(f"Raw frame ({len(frame)} bytes): {frame.hex()}")

                # Decode KISS
                decoded = decode_kiss_frame(frame)
                if decoded:
                    print(f"Decoded packet ({len(decoded)} bytes): {decoded.hex()}")
                    analyze_packet(decoded)
                else:
                    print("  Failed to decode KISS frame")

                print("=" * 70)
                print()

        time.sleep(0.01)

except serial.SerialException as e:
    print(f"\nSerial port error: {e}")
    print(f"\nMake sure:")
    print(f"  1. ESP32 Serial2 (GPIO16/17) is connected to USB-UART adapter")
    print(f"  2. The port {SERIAL_PORT} is correct")
    print(f"  3. You have permission to access the port")
    sys.exit(1)
except KeyboardInterrupt:
    print("\n\nSniffer stopped.")
    ser.close()
    sys.exit(0)
