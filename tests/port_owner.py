import psutil
ports = [r"COM16", r"COM22"]
for proc in psutil.process_iter(['pid','name']):
    try:
        files = proc.open_files()
    except Exception:
        continue
    for f in files:
        for p in ports:
            if p.lower() in f.path.lower():
                print(f"PID {proc.pid} ({proc.name()}) has {f.path}")
