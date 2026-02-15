from pathlib import Path
import sys
p=Path('.pio/build/ttgo-minimal/lib3f4/Monocypher/monocypher.c.o')
if not p.exists():
    print('MISSING',p)
    sys.exit(2)
b=p.read_bytes()
if b[:4]!=b'\x7fELF':
    print('NOT ELF')
    sys.exit(1)
em = b[18] | (b[19]<<8)
print('e_machine=',em)
print('e_machine_hex=0x%02x'%em)
print('class=',b[4])
