import serial

ser = serial.Serial("/dev/ttyS0", 115200, timeout=1)
print("Esperando mensagens do Heltec...")

while True:
    linha = ser.readline().decode(errors="ignore").strip()
    if not linha:
        continue
    print("Recebido:", linha)
    if linha == "1":
        ser.write(b"foi\n")
        print("Respondi: foi")
