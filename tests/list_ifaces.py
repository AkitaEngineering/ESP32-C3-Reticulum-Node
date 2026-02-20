#!/usr/bin/env python3
"""Start a Reticulum instance and print all configured interfaces.

Useful to verify that the PC-side RNS sees the serial ports and other
interfaces that are up.  It also prints brief statistics for each one.
"""

import RNS
import time

print("Launching Reticulum...")
RNS.Reticulum()

# add serial interfaces so they appear in the listing
for port in ["COM16", "COM19", "COM22"]:
    try:
        cfg = {"type": "SerialInterface", "name": port, "port": port, "speed": 115200}
        from RNS.Interfaces import SerialInterface
        iface = SerialInterface.SerialInterface(RNS.Transport, cfg)
        RNS.Transport.interfaces.append(iface)
        iface.final_init()
        print(f"opened SerialInterface for {port}")
    except Exception as e:
        print(f"could not open {port}: {e}")

# wait a moment for them to settle
time.sleep(1)

print("\n=== Interfaces ===")
for iface in RNS.Transport.interfaces:
    print(f"{iface}: online={iface.online} rxb={iface.rxb} txb={iface.txb}")
    if hasattr(iface, 'port'):
        print(f"    port={iface.port} speed={iface.speed}")

print("done")
