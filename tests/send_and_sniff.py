#!/usr/bin/env python3
"""Send a packet over Reticulum and print any incoming packets.

This script configures a local Reticulum instance with two serial
interfaces (COM19 and COM22) and attaches a handler to display any
received packets.  It then sends a single message to the PLAIN
destination ["esp32","node"] and exits after a short delay.

Usage: python tests/send_and_sniff.py
"""

import time
import RNS
from RNS.Interfaces import SerialInterface

# initialize the Reticulum subsystem
print("Starting Reticulum...")
RNS.Reticulum()

# add serial interfaces for the candidate COM ports
for port in ["COM16", "COM19", "COM22"]:
    try:
        cfg = {"type": "SerialInterface", "name": port, "port": port, "speed": 115200}
        iface = SerialInterface.SerialInterface(RNS.Transport, cfg)
        RNS.Transport.interfaces.append(iface)
        iface.final_init()
        print(f"Opened Reticulum serial interface on {port}")
    except Exception as e:
        print(f"could not open {port}, skipping: {e}")

# register a simple packet handler that prints info for anything we get

def incoming_packet(packet):
    print("\n=== Incoming packet ===")
    print(f"destination hash: {packet.destination_hash.hex() if packet.destination_hash else 'None'}")
    print(f"payload: {packet.data}")

# hook into Transport's packet reception by attaching to RNS.Transport.packet_receive_callback
# the callback gets called with an RNS.Packet instance

def transport_callback(pkt):
    incoming_packet(pkt)
    return True

RNS.Transport.packet_receive_callback = transport_callback

# give interfaces a moment to warm up
print("waiting for interfaces to become online...")
time.sleep(2)

# construct and send a test packet
print("sending test packet")
dest = RNS.Destination(None, RNS.Destination.OUT, RNS.Destination.PLAIN, "esp32", "node")
# ping payload so the ESP32 node should respond with a pong string
pkt = RNS.Packet(dest, b"ping")
pkt.send()
print("(sent ping, waiting for pong replies)")

# keep running long enough to possibly receive a response
for i in range(10):
    time.sleep(1)

print("done")
