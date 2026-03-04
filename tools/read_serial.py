import serial, time
s=serial.Serial('COM22',115200,timeout=1)
print('opened', s.name)
start=time.time()
while time.time()-start<5:
    data=s.read(1024)
    if data:
        try:
            print(data.decode('utf-8',errors='replace'), end='')
        except Exception as e:
            print(repr(data))
s.close()
print('\ndone')
