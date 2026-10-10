import serial, time, sys

port = 'COM5'
baud = 115200
out_file = r'd:\shangke\zhou\week1\serial_output_new.txt'

try:
    s = serial.Serial(port, baud, timeout=2)
except serial.SerialException as e:
    print(f"ERROR: {e}")
    sys.exit(1)

# Send a reset to get fresh boot output
s.dtr = False
time.sleep(0.1)
s.dtr = True
time.sleep(1)

lines = []
deadline = time.time() + 20
while time.time() < deadline:
    try:
        raw = s.readline()
        line = raw.decode('utf-8', 'replace').strip()
        if line:
            lines.append(line)
    except Exception:
        pass

s.close()

with open(out_file, 'w', encoding='utf-8') as f:
    f.write('\n'.join(lines))
print(f"Saved {len(lines)} lines")
for l in lines[:40]:
    print(l)