import serial, time, sys

if len(sys.argv) < 2:
    print('Usage: python serial_read.py <COM port> [duration]')
    sys.exit(1)

port = sys.argv[1]
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

ser = serial.Serial(port, 115200, timeout=1)
print(f'Listening on {port} for {dur} seconds...')
start = time.time()
while time.time() - start < dur:
    line = ser.readline()
    if line:
        try:
            print(line.decode('utf-8', errors='ignore').rstrip())
        except Exception:
            print(line)
    time.sleep(0.1)
ser.close()
