"""启动服务器 + 网络诊断，结果写入 _diag.txt"""
import subprocess, time, urllib.request, json, os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_diag.txt")
results = []

# === 1. 启动服务器 ===
proc = subprocess.Popen(
    ["python", "-m", "uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"],
    cwd=r"d:\shangke\zhou\server",
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
)
time.sleep(4)
running = proc.poll() is None
results.append(f"[1] 服务器 PID={proc.pid}, 运行中={running}")

# === 2. 查本机IP ===
import socket
try:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    my_ip = s.getsockname()[0]
    s.close()
except:
    my_ip = "未知"
results.append(f"[2] 本机局域网 IP: {my_ip}")

# === 3. localhost 测试 ===
for name, url in [
    ("localhost /",          "http://127.0.0.1:8000/"),
    ("localhost /api/data",  "http://127.0.0.1:8000/api/data?limit=2"),
    ("localhost /api/ingest POST", None),
]:
    if url:
        try:
            r = urllib.request.urlopen(url, timeout=3)
            body = r.read()
            results.append(f"[3] {name}: OK ({len(body)}B)")
        except Exception as e:
            results.append(f"[3] {name}: FAIL - {e}")
    else:
        # POST test
        try:
            payload = json.dumps({
                "device_id": "diag-test",
                "sensor": "qma7981",
                "ts_device": time.time(),
                "ax": 0.01, "ay": -0.02, "az": 0.98,
                "unit": "g",
                "raw": [100, -200, 8000],
                "status": "ok"
            }).encode()
            req = urllib.request.Request(
                "http://127.0.0.1:8000/api/ingest",
                data=payload,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            r = urllib.request.urlopen(req, timeout=3)
            d = json.loads(r.read())
            results.append(f"[3] localhost /api/ingest POST: OK (id={d.get('id')})")
        except Exception as e:
            results.append(f"[3] localhost /api/ingest POST: FAIL - {e}")

# === 4. LAN IP 测试 ===
for name, url in [
    (f"LAN({my_ip}) /",         f"http://{my_ip}:8000/"),
    (f"LAN({my_ip}) /api/data", f"http://{my_ip}:8000/api/data?limit=1"),
]:
    try:
        r = urllib.request.urlopen(url, timeout=3)
        body = r.read()
        results.append(f"[4] {name}: OK ({len(body)}B)")
    except Exception as e:
        results.append(f"[4] {name}: FAIL - {e}")

# === 5. 检查防火墙规则 ===
fw_ok = os.system("netsh advfirewall firewall show rule name=EgoLink-8000 >nul 2>&1") == 0
results.append(f"[5] 防火墙规则 EgoLink-8000: {'已存在' if fw_ok else '❌ 不存在'}")

# === 6. 检查网络类型 ===
ret = subprocess.run(
    ["powershell", "-NoProfile", "-Command",
     "(Get-NetConnectionProfile | Where-Object {$_.IPv4Connectivity -eq 'Internet'}).NetworkCategory"],
    capture_output=True, text=True, timeout=5
)
net_type = ret.stdout.strip()
results.append(f"[6] 网络类型: {net_type if net_type else '未知'}")
if "Public" in net_type:
    results.append("    ⚠️ 公用网络! ESP32 无法连接！需改为专用网络")

# === 7. ESP32 Kconfig 中的 SERVER_URL ===
kconfig_path = r"d:\shangke\zhou\week2\esp32_firmware\main\Kconfig.projbuild"
if os.path.isfile(kconfig_path):
    with open(kconfig_path) as f:
        for line in f:
            if "default" in line and "http" in line:
                results.append(f"[7] ESP32 SERVER_URL: {line.strip()}")
                break

# === 8. 确认刚POST的数据入了数据库 ===
try:
    r = urllib.request.urlopen("http://127.0.0.1:8000/api/data?device_id=diag-test&limit=1", timeout=3)
    d = json.loads(r.read())
    recs = d.get("data", [])
    if recs:
        results.append(f"[8] 数据库验证: OK (最新 id={recs[0].get('id')}, device={recs[0].get('device_id')})")
    else:
        results.append(f"[8] 数据库验证: ❌ 没有找到 diag-test 数据")
except Exception as e:
    results.append(f"[8] 数据库验证: FAIL - {e}")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(results))

# 不杀服务器，保持运行
print(f"结果已写入: {OUT}")
