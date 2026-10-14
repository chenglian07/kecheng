"""启动服务器为独立进程，然后运行诊断"""
import subprocess, time, sys, os

# 启动服务器（detached）
proc = subprocess.Popen(
    [sys.executable, "-m", "uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"],
    cwd=r"d:\shangke\zhou\server",
    creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
)
print(f"Server PID: {proc.pid}")

# 等待服务器就绪
time.sleep(5)
if proc.poll() is not None:
    print(f"Server exited with code {proc.returncode}")
    sys.exit(1)
print("Server running!")

# 写入PID文件
with open(r"d:\shangke\zhou\_server.pid", "w") as f:
    f.write(str(proc.pid))

# 测试
import urllib.request, json

tests = [
    ("localhost GET /", "http://127.0.0.1:8000/"),
    ("localhost GET /api/data", "http://127.0.0.1:8000/api/data?limit=2"),
    ("localhost POST /api/ingest", None),
    ("localhost POST /api/imu", None),
]

results = []
for name, url in tests:
    if url:
        try:
            r = urllib.request.urlopen(url, timeout=3)
            body = r.read()
            results.append(f"{name}: OK ({len(body)}B)")
        except Exception as e:
            results.append(f"{name}: FAIL - {e}")
    else:
        ep = "/api/ingest" if "ingest" in name else "/api/imu"
        try:
            payload = json.dumps({
                "device_id": f"diag-{name.split()[-1]}",
                "sensor": "qma7981",
                "ts_device": time.time(),
                "ax": 0.01, "ay": -0.02, "az": 0.98,
                "unit": "g", "raw": [100, -200, 8000], "status": "ok"
            }).encode()
            req = urllib.request.Request(
                f"http://127.0.0.1:8000{ep}",
                data=payload,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            r = urllib.request.urlopen(req, timeout=3)
            d = json.loads(r.read())
            results.append(f"{name}: OK (id={d.get('id')})")
        except Exception as e:
            results.append(f"{name}: FAIL - {e}")

# LAN IP 测试
import socket
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    my_ip = s.getsockname()[0]
    s.close()
    r = urllib.request.urlopen(f"http://{my_ip}:8000/api/data?limit=1", timeout=3)
    d = json.loads(r.read())
    results.append(f"LAN({my_ip}) GET /api/data: OK ({len(d.get('data',[]))} records)")
except Exception as e:
    results.append(f"LAN test: FAIL - {e}")

# 写结果
with open(r"d:\shangke\zhou\_diag.txt", "w", encoding="utf-8") as f:
    f.write("\n".join(results))

for r in results:
    print(r)
print(f"\nServer running as PID {proc.pid} - don't close it!")
