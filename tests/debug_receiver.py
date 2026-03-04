#!/usr/bin/env python3
"""
Debug receiver that logs everything RNS processes
"""
import RNS
import sys

# Set up logging
RNS.loglevel = RNS.LOG_DEBUG

print("=" * 70)
print("DEBUG RECEIVER - Will log all RNS activity")
print("=" * 70)

# Create Reticulum instance (will connect to rnsd if running)
reticulum = RNS.Reticulum(loglevel=RNS.LOG_DEBUG)

print(f"\nReticulum started. Is shared instance: {reticulum.is_shared_instance}")

# Create destination
print("\nCreating PLAIN destination ['esp32', 'node']")
dest = RNS.Destination(None, RNS.Destination.IN, RNS.Destination.PLAIN, "esp32", "node")

print(f"Destination hash: {dest.hash.hex()}")
print(f"Destination type: {dest.type}")
print(f"Destination direction: {'IN' if dest.direction == RNS.Destination.IN else 'OUT'}")

# Set callback
def packet_received(packet_data, packet):
    print("\n" + "=" * 70)
    print("PACKET RECEIVED!")
    print("=" * 70)
    print(f"Data: {packet_data}")
    print(f"Data (hex): {packet_data.hex()}")
    print(f"Data (text): {packet_data.decode('utf-8', errors='replace')}")
    print(f"Packet object: {packet}")
    if hasattr(packet, 'packet_hash'):
        print(f"Packet hash: {packet.packet_hash.hex()}")
    print("=" * 70)

dest.set_packet_callback(packet_received)

print("\n" + "=" * 70)
print("Listening for packets...")
print("Press Ctrl+C to exit")
print("=" * 70)

try:
    while True:
        import time
        time.sleep(1)
except KeyboardInterrupt:
    print("\nExiting...")
    sys.exit(0)
