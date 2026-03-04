#!/usr/bin/env python3
"""
Helper script to calculate the destination hash for a PLAIN destination.
This hash must be used in the ESP32 code to send to the correct destination.
"""
import RNS

reticulum = RNS.Reticulum()

dest = RNS.Destination(None, RNS.Destination.IN, RNS.Destination.PLAIN, "esp32", "node")
dest.set_packet_callback(lambda packet, data: print(packet, data,"\n\nWaiting for messages ... press any key to stop\n")        )

full_hash = dest.hash

# Print the hash in various formats
print("Destination hash for PLAIN destination ['esp32', 'node']:")
print(f"\nFull hash (16 bytes): {full_hash.hex()}")
print(f"\nC array format for ESP32 (16 bytes):") 
print("uint8_t dest_hash[16] = {", end="")
print(", ".join([f"0x{b:02X}" for b in full_hash]), end="")
print("};")
print(f"\nHash bytes: {list(full_hash)}")




input("Waiting for messages ... press any key to stop\n")        