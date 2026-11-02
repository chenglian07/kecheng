"""
run_and_test.py - 启动 FastAPI 服务器并运行测试
"""
import subprocess
import sys
import time
import os

os.chdir(os.path.dirname(os.path.abspath(__file__)))

# 启动服务器
print("[*] 启动 uvicorn 服务器...")
server = subprocess.Popen(
    [sys.executable, "-m", "uvicorn", "main:app", "--host", "127.0.0.1", "--port", "8000"],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE
)

# 等待服务器就绪
import urllib.request
for i in range(20):
    time.sleep(0.5)
    try:
        r = urllib.request.urlopen("http://127.0.0.1:8000/api/health")
        print(f"[✓] 服务器已就绪 (等待 {(i+1)*0.5:.1f}s)")
        break
    except:
        continue
else:
    print("[✗] 服务器启动超时!")
    server.kill()
    sys.exit(1)

# 运行测试
print("[*] 运行测试脚本...")
result = subprocess.run([sys.executable, "test_week2.py"])

# 清理
server.terminate()
server.wait(timeout=5)
sys.exit(result.returncode)