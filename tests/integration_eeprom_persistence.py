#!/usr/bin/env python3
"""
Integration test (host-side):
- Open serial to device
- Read `Node Address:` from boot output
- Soft-reset device via DTR toggle
- Ensure the Node Address printed after reboot matches the first one

Usage: python3 tests/integration_eeprom_persistence.py --port COM22
"""
import argparse
import re
import sys
import time

import serial

LINE_TIMEOUT = 15.0

addr_re = re.compile(r"Node Address:\s*([0-9A-Fa-f]{2})+")
hex_pair_re = re.compile(r"([0-9A-Fa-f]{2})")


def read_node_address(ser, timeout=LINE_TIMEOUT):
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        try:
            s = line.decode('utf-8', errors='ignore').strip()
        except Exception:
            s = repr(line)
        # Debug print
        # print('RX:', s)
        if 'Node Address:' in s:
            # Extract hex bytes
            hexs = hex_pair_re.findall(s)
            if len(hexs) >= 8:
                addr = bytes(int(h, 16) for h in hexs[:8])
                return addr
    return None


def toggle_dtr_reset(ser):
    # Toggle DTR to force a reset on most devkit boards
    ser.dtr = False
    time.sleep(0.100)
    ser.dtr = True
    time.sleep(0.100)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--port', '-p', required=True, help='Serial port (e.g. COM22 or /dev/ttyUSB0)')
    p.add_argument('--baud', '-b', default=115200, type=int)
    p.add_argument('--tries', default=1, type=int)
    args = p.parse_args()

    for attempt in range(args.tries):
        try:
            with serial.Serial(args.port, args.baud, timeout=1) as ser:
                ser.reset_input_buffer()
                # Read until we see Node Address
                print('Waiting for first Node Address...')
                addr1 = read_node_address(ser)
                if not addr1:
                    print('ERROR: timeout waiting for Node Address on first boot')
                    return 2
                print('First Node Address:', ' '.join(f"{b:02X}" for b in addr1))

                # Reset device via DTR toggle
                print('Toggling DTR to reset device...')
                toggle_dtr_reset(ser)

                # Wait for Node Address after reboot
                print('Waiting for Node Address after reboot...')
                addr2 = read_node_address(ser)
                if not addr2:
                    print('ERROR: timeout waiting for Node Address after reboot')
                    return 3
                print('Second Node Address:', ' '.join(f"{b:02X}" for b in addr2))

                if addr1 == addr2:
                    print('OK: Node Address persisted across reboot')
                    return 0
                else:
                    print('FAIL: Node Address changed across reboot')
                    return 4
        except serial.SerialException as e:
            print('Serial error:', e)
            time.sleep(1)
    return 5


if __name__ == '__main__':
    sys.exit(main())
