#!/usr/bin/env python3
"""
Reticulum Packet Sniffer - Shows RAW packets and decoded information
This tool helps debug packet communication between ESP32 and Python
"""
import RNS
import sys
import time

# Initialize Reticulum
reticulum = RNS.Reticulum(loglevel=RNS.LOG_EXTREME)

print("=" * 70)
print("RETICULUM PACKET SNIFFER")
print("=" * 70)
print(f"Listening on all interfaces...")
print(f"Press Ctrl+C to exit\n")

# Create a destination to receive packets
dest = RNS.Destination(None, RNS.Destination.IN, RNS.Destination.PLAIN, "esp32", "node")

def packet_callback(data, packet):
    """Called when a packet is received"""
    print("\n" + "=" * 70)
    print(f"PACKET RECEIVED at {time.strftime('%H:%M:%S')}")
    print("=" * 70)

    # Packet info
    print(f"Packet object: {packet}")
    print(f"Destination hash: {packet.destination_hash.hex() if packet.destination_hash else 'None'}")
    print(f"Packet hash: {packet.packet_hash.hex() if packet.packet_hash else 'None'}")
    print(f"Transport ID: {packet.transport_id.hex() if packet.transport_id else 'None'}")
    print(f"Hops: {packet.hops}")

    # Data content
    print(f"\nData length: {len(data)} bytes")
    print(f"Data (hex): {data.hex()}")
    try:
        print(f"Data (text): {data.decode('utf-8', errors='replace')}")
    except:
        print(f"Data (text): <not UTF-8>")

    # Raw packet data if available
    if hasattr(packet, 'raw'):
        print(f"\nRaw packet length: {len(packet.raw)} bytes")
        print(f"Raw packet (hex): {packet.raw.hex()}")

    print("=" * 70)

# Register callback
dest.set_packet_callback(packet_callback)

print(f"Destination created:")
print(f"  Type: PLAIN")
print(f"  Aspects: ['esp32', 'node']")
print(f"  Hash (16 bytes): {dest.hash.hex()}")
print(f"  Address (8 bytes): {dest.hash[:8].hex()}")
print(f"\nWaiting for packets...\n")

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("\n\nSniffer stopped.")
    sys.exit(0)
