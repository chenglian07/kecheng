"""串口监视器 - 复位板子并抓取启动日志"""
import serial
import time

port = "COM5"
baud = 115200

try:
    ser = serial.Serial(port, baud, timeout=1)
    print(f"Connected to {port} at {baud} baud")
    
    # Toggle RTS to reset the board
    print("Resetting board via RTS...")
    ser.dtr = False
    ser.rts = False
    time.sleep(0.1)
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False
    
    print("Reading boot log for 20 seconds...\n")
    
    output_lines = []
    start = time.time()
    while time.time() - start < 20:
        line = ser.readline()
        if line:
            try:
                text = line.decode('utf-8', errors='replace').rstrip()
                if text:
                    print(text)
                    output_lines.append(text)
            except:
                pass
    
    ser.close()
    
    # Write output to file
    with open(r'd:\shangke\zhou\week1\serial_output.txt', 'w', encoding='utf-8') as f:
        for l in output_lines:
            f.write(l + '\n')
    
    print(f"\n--- Done ({len(output_lines)} lines captured) ---")
except serial.SerialException as e:
    print(f"Serial error: {e}")
    with open(r'd:\shangke\zhou\week1\serial_output.txt', 'w') as f:
        f.write(f"ERROR: {e}\n")


