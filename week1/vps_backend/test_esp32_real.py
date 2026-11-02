"""
test_esp32_real.py - 真实 ESP32 联调测试脚本
============================================
用法：
  1. 先启动后端: cd d:\\shangke\\zhou\\week1\\vps_backend && python run_and_test.py
     或单独启动: python -m uvicorn main:app --host 0.0.0.0 --port 8000
  2. ESP32 已烧录第2周固件并运行中（串口 monitor 打开）
  3. 运行本脚本: python test_esp32_real.py

脚本会：
  - 发送一条"采集一次"指令给后端
  - 等待 ESP32 自己拉取、执行、回传
  - 轮询查询直到 ESP32 完成（或超时 30 秒）
  - 打印最终的 IMU 数据（这才是 ESP32 真实采集的！）

同时观察 ESP32 串口，应看到：
  📩 拉取到指令
  🔬 单采完成
  ✅ 结果已回传
"""

import urllib.request
import json
import time
import sys

BASE = "http://127.0.0.1:8000"
DEVICE_ID = "my-esp32"

def req(method, path, body=None):
    url = f"{BASE}{path}"
    data = json.dumps(body).encode() if body else None
    r = urllib.request.Request(url, data=data, method=method)
    if body:
        r.add_header("Content-Type", "application/json")
    try:
        resp = urllib.request.urlopen(r)
        return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())

print("=" * 60)
print("  真实 ESP32 联调测试")
print("  设备ID: " + DEVICE_ID)
print("=" * 60)

# 步骤1: 确认后端在线
print("\n[1] 检查后端...")
try:
    code, data = req("GET", "/api/health")
    print(f"    ✅ 后端在线 (server_time={data['server_time']})")
except Exception as e:
    print(f"    ❌ 后端不在线: {e}")
    print("    请先启动: cd vps_backend && python -m uvicorn main:app --port 8000")
    sys.exit(1)

# 步骤2: 发送采集指令（模拟 Web 点击按钮）
print("\n[2] 📤 发送采集指令（模拟网页点击'采集一次最新数据'）...")
code, data = req("POST", "/api/tasks/collect", {"device_id": DEVICE_ID})
request_id = data["request_id"]
print(f"    ✅ 指令已发送!")
print(f"    request_id = {request_id}")
print(f"    ⏳ 等待 ESP32 拉取指令...")

# 步骤3: 轮询等待 ESP32 完成
print("\n[3] 🔄 轮询任务状态（等待 ESP32 完成采集）...")
print("    （同时观察 ESP32 串口，应看到 '📩 拉取到指令' 和 '🔬 单采完成'）")

start_time = time.time()
timeout = 30  # 秒

while True:
    elapsed = time.time() - start_time
    if elapsed > timeout:
        print(f"\n    ❌ 超时 ({timeout}s)！ESP32 可能未运行或 WiFi 未连接")
        break

    code, data = req("GET", f"/api/tasks/{request_id}")
    status = data["status"]

    if status == "submitted":
        print(f"    [{elapsed:5.1f}s] 状态: submitted (等待 ESP32 拉取...)")
    elif status == "received":
        print(f"    [{elapsed:5.1f}s] 状态: received (ESP32 已收到，正在采集...)")
    elif status == "completed":
        print(f"    [{elapsed:5.1f}s] ✅ 状态: completed!")
        break
    elif status == "failed":
        print(f"    [{elapsed:5.1f}s] ❌ 状态: failed - {data['error_message']}")
        break
    elif status == "timeout":
        print(f"    [{elapsed:5.1f}s] ❌ 状态: timeout - {data['error_message']}")
        break

    time.sleep(0.3)

# 步骤4: 打印 ESP32 真实采集的数据
if data["status"] == "completed":
    imu = data["imu_data"]
    print(f"\n{'=' * 60}")
    print(f"  🎉 ESP32 远程采集成功！")
    print(f"{'=' * 60}")
    print(f"  request_id:  {data['request_id']}")
    print(f"  设备ID:      {data['device_id']}")
    print(f"  采集时间线:")
    print(f"    创建时间:    {data['created_at']}")
    print(f"    ESP32收到:   {data['received_at']}")
    print(f"    采集完成:    {data['completed_at']}")
    print(f"  IMU 加速度 (mg):")
    print(f"    X 轴:  {imu['ax_mg']:+10.1f} mg")
    print(f"    Y 轴:  {imu['ay_mg']:+10.1f} mg")
    print(f"    Z 轴:  {imu['az_mg']:+10.1f} mg")
    print(f"  原始值:")
    print(f"    raw_x: {imu['raw_x']:+6d}")
    print(f"    raw_y: {imu['raw_y']:+6d}")
    print(f"    raw_z: {imu['raw_z']:+6d}")
    print(f"  设备时间戳: {imu['timestamp']}")
    print(f"  Chip ID:    0x{imu['chip_id']:02X}")
    print(f"{'=' * 60}")
    print(f"\n  ✅ 请检查 ESP32 串口是否有:")
    print(f"     '📩 拉取到指令: {request_id[:12]}...'")
    print(f"     '🔬 单采完成: raw={imu['raw_x']}/{imu['raw_y']}/{imu['raw_z']}'")
    print(f"     '✅ 结果已回传'")
else:
    print(f"\n  ❌ 测试未通过，最终状态: {data['status']}")
    print(f"     错误信息: {data.get('error_message', '无')}")

print("\n完成。")