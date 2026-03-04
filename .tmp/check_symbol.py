p = '.pio\\build\\esp32-c3-minimal\\firmware.elf'
with open(p, 'rb') as f:
    data = f.read()
print('_svfprintf_r' in data)