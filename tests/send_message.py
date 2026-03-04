#!/usr/bin/env python3
"""
Simple one-shot script to send a message to the ESP32 via Reticulum.
Sends a default message or accepts ayou don't need the device address! When sending to a PLAIN destination:
You send to the destination path ["esp32", "node"]
Any ESP32 node listening on that destination will receive it
The node address 74058EAB72F84342 is the node's identity, but messages route via the destination path custom message as argument.
"""
import RNS
import sys

# Initialize Reticulum
print("Initializing Reticulum...")
RNS.Reticulum()

# Create destination for ESP32 (PLAIN destination with path ["esp32", "node"])
dest = RNS.Destination(None, RNS.Destination.OUT, RNS.Destination.PLAIN, "esp32", "node")

# Get message from command line argument or use default
if len(sys.argv) > 1:
    message = " ".join(sys.argv[1:])
else:
    message = "HI from pc"

# Send packet
print(f"Sending message to ESP32: '{message}'")
packet = RNS.Packet(dest, message.encode())
packet.send()
print("✓ Message sent successfully!")
print(f"\nDestination: ['esp32', 'node']")
print(f"Destination hash: {dest.hash.hex()}")
