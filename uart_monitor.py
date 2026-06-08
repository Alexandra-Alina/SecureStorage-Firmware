import serial, time

print("=== Board UART Monitor (COM6 115200) ===")
print("Press Ctrl+C to stop")
print()

with serial.Serial('COM6', 115200, timeout=1) as ser:
    while True:
        line = ser.readline()
        if line:
            try:
                print(line.decode('utf-8', errors='replace').rstrip(), flush=True)
            except:
                pass
