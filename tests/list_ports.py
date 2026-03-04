import serial.tools.list_ports as lp

for p in lp.comports():
    print(p.device, "-", p.description)
