import serial, time
s = serial.Serial('COM16', 115200, timeout=1)
print('opened', s.name)
try:
    s.setDTR(False)
    time.sleep(0.05)
    s.setDTR(True)
    print('toggled DTR')
except Exception as e:
    print('DTR toggle failed', e)
start=time.time()
while time.time()-start<8:
    data=s.read(1024)
    if data:
        try:
            print(data.decode('utf-8','replace'), end='')
        except Exception:
            print(repr(data))
s.close()
print('\ndone')
