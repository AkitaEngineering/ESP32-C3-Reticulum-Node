import serial, time
s = serial.Serial('COM16', 115200, timeout=1)
print('opened', s.name)
start = time.time()
while time.time() - start < 12:
    data = s.read(1024)
    if data:
        try:
            print(data.decode('utf-8', errors='replace'), end='')
        except Exception:
            print(repr(data))
s.close()
print('\ndone')
