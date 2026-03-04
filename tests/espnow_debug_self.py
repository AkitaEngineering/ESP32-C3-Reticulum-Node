"""Send command over same serial port and listen for replies.
Usage: python tests/espnow_debug_self.py <COM port> <command>
"""
import sys
import serial
import time

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


def main():
    if len(sys.argv) < 3:
        print("Usage: espnow_debug_self.py <COM port> <command>")
        return
    port = sys.argv[1]
    cmd = sys.argv[2].encode('ascii')

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        print(f"Failed to open {port}: {e}")
        return

    payload = bytes([0] * 8) + cmd
    frame = make_kiss_frame(payload)
    ser.write(frame)
    print(f"Sent command '{cmd.decode()}' to device on {port}")
    start = time.time()
    while time.time() - start < 5:
        data = ser.readline()
        if data:
            try:
                print(data.decode('utf-8', errors='ignore').rstrip())
            except Exception:
                print(data)
    ser.close()

if __name__ == '__main__':
    main()
