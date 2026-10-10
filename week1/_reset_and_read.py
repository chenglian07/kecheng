import serial, time, sys

try:
    s = serial.Serial('COM5', 115200, timeout=1)
except Exception as e:
    with open('d:/shangke/zhou/week1/serial_boot8.txt', 'w') as f:
        f.write(f'ERROR: {e}\n')
    sys.exit(1)

# Toggle DTR to reset
s.setDTR(False)
time.sleep(0.1)
s.setDTR(True)

# Wait for boot + read for 12 seconds
time.sleep(4)
lines = []
start = time.time()
while time.time() - start < 12:
    line = s.readline()
    if line:
        lines.append(line.decode('utf-8', errors='replace'))

s.close()

with open('d:/shangke/zhou/week1/serial_boot8.txt', 'w', encoding='utf-8') as f:
    f.writelines(lines)
    f.write(f'\n--- {len(lines)} lines captured ---\n')
