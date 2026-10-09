import serial, time, sys, subprocess

# Reset board via DTR
try:
    s = serial.Serial('COM4', 115200, timeout=1)
except Exception as e:
    with open('d:/shangke/zhou/week1/serial_boot9.txt', 'w') as f:
        f.write(f'ERROR opening port: {e}\n')
    sys.exit(1)

# Toggle DTR to reset
s.dtr = False
time.sleep(0.1)
s.dtr = True
time.sleep(0.1)
s.reset_input_buffer()

# Read serial output for 25 seconds (enough for boot + WiFi + first readings)
lines = []
start = time.time()
while time.time() - start < 25:
    line = s.readline()
    if line:
        lines.append(line.decode('utf-8', errors='replace'))

s.close()

with open('d:/shangke/zhou/week1/serial_boot9.txt', 'w', encoding='utf-8') as f:
    f.writelines(lines)
    f.write(f'\n--- {len(lines)} lines captured ---\n')
