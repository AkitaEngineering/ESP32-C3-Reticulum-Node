import RNS, time
RNS.Reticulum()

dest = RNS.Destination(None, RNS.Destination.OUT, RNS.Destination.PLAIN, "esp32", "node")
while True:
    packet = RNS.Packet(dest, f"Hi from python {int(time.time())}".encode())
    packet.send()
    print("Enviado")
    time.sleep(10)