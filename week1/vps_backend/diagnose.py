"""
diagnose.py - 一键诊断脚本：排查浏览器无法访问的问题
运行方式：在 vps_backend 目录下执行  python diagnose.py
"""
import urllib.request
import json
import os
import sys
import subprocess

# Fix Windows console encoding
if sys.platform == 'win32':
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')

print("=" * 60)
print("  [DIAG] Week2 Backend Diagnostics")
print("=" * 60)

# ---- 1. 检查前端文件是否存在 ----
print("\n[1] 检查前端文件路径...")
frontend_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "web_frontend")
index_path = os.path.join(frontend_dir, "index.html")
print(f"    FRONTEND_DIR = {os.path.abspath(frontend_dir)}")
print(f"    index.html   = {os.path.abspath(index_path)}")
if os.path.exists(index_path):
    size = os.path.getsize(index_path)
    print(f"    ✅ 文件存在 ({size} bytes)")
else:
    print(f"    ❌ 文件不存在！这就是白屏原因！")
    sys.exit(1)

# ---- 2. 检查 main.py 能否正常导入 ----
print("\n[2] 检查 main.py 导入...")
try:
    from main import app
    routes = [r.path for r in app.routes]
    print(f"    ✅ main.py 导入成功")
    print(f"    已注册路由: {routes}")
    has_root = "/" in routes
    print(f"    GET / 路由: {'✅ 存在' if has_root else '❌ 缺失！'}")
except Exception as e:
    print(f"    ❌ 导入失败: {e}")
    sys.exit(1)

# ---- 3. 测试 API 访问 ----
print("\n[3] 测试本地 API 访问...")
tests = [
    ("http://127.0.0.1:8000/api/health", "API 健康检查"),
    ("http://127.0.0.1:8000/",          "前端页面"),
]
for url, desc in tests:
    try:
        r = urllib.request.urlopen(url, timeout=3)
        ct = r.headers.get("Content-Type", "")
        size = len(r.read())
        print(f"    ✅ {desc}: HTTP {r.status} ({size} bytes, {ct})")
    except urllib.error.URLError as e:
        print(f"    ❌ {desc}: {e.reason}")
        if "10061" in str(e):
            print(f"       → 后端未启动或端口不对！请先启动：")
            print(f"         python -m uvicorn main:app --host 0.0.0.0 --port 8000")
    except Exception as e:
        print(f"    ❌ {desc}: {e}")

# ---- 4. 检查端口监听状态 ----
print("\n[4] 检查 8000 端口监听状态...")
try:
    result = subprocess.run(
        ["netstat", "-ano"],
        capture_output=True, text=True, timeout=5
    )
    lines = [l for l in result.stdout.split("\n") if ":8000" in l]
    if lines:
        for l in lines:
            print(f"    {l.strip()}")
        if any("0.0.0.0:8000" in l for l in lines):
            print(f"    ✅ 正在监听 0.0.0.0:8000（所有网卡）")
        elif any("127.0.0.1:8000" in l for l in lines):
            print(f"    ⚠️ 只监听了 127.0.0.1:8000！局域网无法访问！")
            print(f"       请用 --host 0.0.0.0 重新启动！")
    else:
        print(f"    ❌ 端口 8000 没有任何监听！后端未启动。")
except Exception as e:
    print(f"    ⚠️ 检查失败: {e}")

# ---- 5. 检查防火墙规则 ----
print("\n[5] 检查 Windows 防火墙...")
try:
    result = subprocess.run(
        ["netsh", "advfirewall", "firewall", "show", "rule", "name=all", "dir=in"],
        capture_output=True, text=True, timeout=10
    )
    if "8000" in result.stdout:
        print(f"    ✅ 发现 8000 端口的防火墙入站规则")
    else:
        print(f"    ⚠️ 未发现 8000 端口的入站规则！")
        print(f"       Windows 防火墙可能阻止了局域网访问。")
        print(f"       请运行以下命令添加规则：")
        print(f'       netsh advfirewall firewall add rule name="FastAPI-8000" dir=in action=allow protocol=TCP localport=8000')
except Exception as e:
    print(f"    ⚠️ 检查失败: {e}")

# ---- 6. 检查代理设置 ----
print("\n[6] 检查系统代理设置...")
try:
    result = subprocess.run(
        ["reg", "query",
         "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
         "/v", "ProxyEnable"],
        capture_output=True, text=True, timeout=5
    )
    if "0x1" in result.stdout:
        print(f"    ⚠️ 系统代理已开启！")
        print(f"       这是最可能的原因：Clash/V2Ray 等代理拦截了浏览器的请求。")
        print(f"       解决方案（选一个）：")
        print(f"       ① 在代理软件的「绕过」列表中添加 10.1.41.154 和 127.0.0.1")
        print(f"       ② 浏览器安装 SwitchyOmega 扩展，对 10.1.41.154 走直连")
        print(f"       ③ 临时关闭系统代理，测试访问后重新开启")
    else:
        print(f"    ✅ 系统代理未开启")
except Exception as e:
    print(f"    ⚠️ 检查失败: {e}")

# ---- 7. 获取本机 IP 确认 ----
print("\n[7] 本机 IP 地址...")
try:
    result = subprocess.run(
        ["ipconfig"],
        capture_output=True, text=True, timeout=5
    )
    for line in result.stdout.split("\n"):
        line = line.strip()
        if "IPv4" in line or "10." in line or "192.168" in line:
            print(f"    {line}")
except Exception as e:
    print(f"    ⚠️ {e}")

print("\n" + "=" * 60)
print("  诊断完成。请根据上方 ⚠️/❌ 项进行修复。")
print("=" * 60)