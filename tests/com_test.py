import serial, time, threading

FEND = 0xC0
CMD_DATA = 0x00


def kiss_escape(data: bytes) -> bytes:
    # simple KISS escape
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


def reader(serial_obj, port_name):
    # serial_obj already opened by caller
    s = serial_obj
    buf = bytearray()
    in_frame = False
    while True:
        b = s.read(1)
        if not b:
            time.sleep(0.05)
            continue
        byte = b[0]
        if byte == FEND:
            if in_frame and buf:
                print(f"[{port_name}] got payload: {buf.hex()}")
                buf.clear()
            in_frame = True
            continue
        if in_frame:
            if byte == 0xDB:
                nxt = s.read(1)
                if nxt and nxt[0] == 0xDC:
                    buf.append(FEND)
                elif nxt and nxt[0] == 0xDD:
                    buf.append(0xDB)
                continue
            buf.append(byte)


def main():
    ports = ["COM16", "COM22"]
    threads = []

    # helper to open with retries (same as above)
    def open_with_retry(port):
        for i in range(10):
            try:
                return serial.Serial(port, 115200, timeout=1)
            except serial.SerialException as e:
                if "Access is denied" in str(e) or "PermissionError" in str(e):
                    print(f"{port} busy (SerialException), retrying...")
                else:
                    raise
                try:
                    import subprocess, shutil
                    hpath = shutil.which("handle.exe")
                    if hpath:
                        out = subprocess.check_output([hpath, port], stderr=subprocess.STDOUT, text=True)
                        print(f"handle output for {port}:\n{out}")
                except Exception:
                    pass
                time.sleep(0.5)
            except PermissionError as e:
                print(f"{port} busy (PermissionError), retrying...")
                time.sleep(0.5)
            except Exception as e:
                raise
        raise IOError(f"Could not open {port} after retries")

    # open both ports once
    serial_objs = {}
    for p in ports:
        try:
            serial_objs[p] = open_with_retry(p)
        except Exception as e:
            print(f"failed to open {p}", e)
            return

    # start reader threads using the opened serial objects
    for p in ports:
        t = threading.Thread(target=reader, args=(serial_objs[p], p), daemon=True)
        t.start()
        threads.append(t)

    time.sleep(1)

    # send simple hello from COM16 to COM22
    try:
        serial_objs["COM16"].write(make_kiss_frame(b"hello from 16"))
        print("sent frame from COM16")
    except Exception as e:
        print("failed to write COM16", e)
    try:
        serial_objs["COM22"].write(make_kiss_frame(b"greetings from 22"))
        print("sent frame from COM22")
    except Exception as e:
        print("failed to write COM22", e)

    time.sleep(5)


if __name__ == '__main__':
    main()
