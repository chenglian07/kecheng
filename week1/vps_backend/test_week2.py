"""
test_week2.py - 第二周接口完整端到端测试
模拟 Web 前端 → 后端 → ESP32 的完整流程
"""
import urllib.request
import json
import time

BASE = "http://127.0.0.1:8000"

def req(method, path, body=None):
    """发送 HTTP 请求并返回 JSON"""
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
print("  第二周 API 端到端测试")
print("=" * 60)

# ---- 测试 1: 健康检查 ----
print("\n[1] GET /api/health")
code, data = req("GET", "/api/health")
print(f"    HTTP {code}: {data}")

# ---- 测试 2: Web 点击"采集一次最新数据" ----
print("\n[2] POST /api/tasks/collect  (模拟 Web 点击按钮)")
code, data = req("POST", "/api/tasks/collect", {"device_id": "my-esp32"})
print(f"    HTTP {code}: {json.dumps(data, ensure_ascii=False)}")
request_id = data["request_id"]
print(f"    ✅ 获取 request_id: {request_id}")

# ---- 测试 3: Web 查询任务状态（此时应为 submitted） ----
print("\n[3] GET /api/tasks/{id}  (Web 前端轮询)")
code, data = req("GET", f"/api/tasks/{request_id}")
print(f"    HTTP {code}: status={data['status']}")
assert data["status"] == "submitted", f"期望 submitted, 实际 {data['status']}"
print(f"    ✅ 状态正确: submitted")

# ---- 测试 4: ESP32 轮询拉取指令 ----
print("\n[4] GET /api/tasks/pending?device_id=my-esp32  (模拟 ESP32 拉取)")
code, data = req("GET", "/api/tasks/pending?device_id=my-esp32")
print(f"    HTTP {code}: task={data['task']}, periodic_report={data['periodic_report']}")
assert data["task"] is not None, "期望有任务, 实际为 None"
assert data["task"]["request_id"] == request_id, "request_id 不匹配"
assert data["periodic_report"] == True, "默认应允许周期上报"
print(f"    ✅ ESP32 成功拉取到任务, periodic_report=True")

# ---- 测试 5: Web 再次查询（此时应为 received） ----
print("\n[5] GET /api/tasks/{id}  (Web 前端轮询, 期望 received)")
code, data = req("GET", f"/api/tasks/{request_id}")
print(f"    HTTP {code}: status={data['status']}")
assert data["status"] == "received", f"期望 received, 实际 {data['status']}"
print(f"    ✅ 状态正确: received (ESP32 已确认接收)")

# ---- 测试 6: ESP32 回传采集结果 ----
print("\n[6] POST /api/tasks/{id}/complete  (模拟 ESP32 采集成功)")
imu_result = {
    "ax_mg": 15.3, "ay_mg": -22.7, "az_mg": 995.1,
    "raw_x": 125, "raw_y": -185, "raw_z": 8150,
    "chip_id": 179, "timestamp": int(time.time())
}
code, data = req("POST", f"/api/tasks/{request_id}/complete", imu_result)
print(f"    HTTP {code}: {json.dumps(data, ensure_ascii=False)}")
assert code == 200, f"期望 200, 实际 {code}"
print(f"    ✅ 采集结果已成功回传")

# ---- 测试 7: Web 最终查询（应为 completed + IMU 数据） ----
print("\n[7] GET /api/tasks/{id}  (Web 前端最终查询)")
code, data = req("GET", f"/api/tasks/{request_id}")
print(f"    HTTP {code}: status={data['status']}")
print(f"    IMU 数据: ax={data['imu_data']['ax_mg']}mg, "
      f"ay={data['imu_data']['ay_mg']}mg, az={data['imu_data']['az_mg']}mg")
print(f"    原始值: raw_x={data['imu_data']['raw_x']}, "
      f"raw_y={data['imu_data']['raw_y']}, raw_z={data['imu_data']['raw_z']}")
assert data["status"] == "completed", f"期望 completed, 实际 {data['status']}"
assert data["imu_data"]["ax_mg"] == 15.3, "IMU 数据不匹配"
print(f"    ✅ 状态 completed, IMU 数据完整且与回传值一致!")

# ---- 测试 8: 暂停周期上报 ----
print("\n[8] POST /api/tasks/periodic  (暂停周期上报)")
code, data = req("POST", "/api/tasks/periodic",
                 {"device_id": "my-esp32", "action": "pause"})
print(f"    HTTP {code}: periodic_report={data['periodic_report']}")
assert data["periodic_report"] == False, "暂停后应为 False"
print(f"    ✅ 周期上报已暂停")

# ---- 测试 9: ESP32 再次拉取（验证 periodic_report=False） ----
print("\n[9] GET /api/tasks/pending  (验证暂停状态传递给 ESP32)")
code, data = req("GET", "/api/tasks/pending?device_id=my-esp32")
print(f"    HTTP {code}: task={data['task']}, periodic_report={data['periodic_report']}")
assert data["task"] is None, "已完成的任务不应再次出现"
assert data["periodic_report"] == False, "ESP32 应收到暂停信号"
print(f"    ✅ ESP32 收到 periodic_report=False, 将暂停周期上报")

# ---- 测试 10: 恢复周期上报 ----
print("\n[10] POST /api/tasks/periodic  (恢复周期上报)")
code, data = req("POST", "/api/tasks/periodic",
                 {"device_id": "my-esp32", "action": "resume"})
print(f"    HTTP {code}: periodic_report={data['periodic_report']}")
assert data["periodic_report"] == True, "恢复后应为 True"
print(f"    ✅ 周期上报已恢复")

# ---- 测试 11: 测试失败上报 ----
print("\n[11] 测试失败路径: 创建任务 → ESP32 上报失败")
code, data = req("POST", "/api/tasks/collect", {"device_id": "my-esp32"})
fail_rid = data["request_id"]
print(f"    创建任务: {fail_rid}")
code, data = req("POST", f"/api/tasks/{fail_rid}/fail",
                 {"error": "sensor_read_failed"})
print(f"    HTTP {code}: {json.dumps(data, ensure_ascii=False)}")
code, data = req("GET", f"/api/tasks/{fail_rid}")
print(f"    查询: status={data['status']}, error={data['error_message']}")
assert data["status"] == "failed", f"期望 failed, 实际 {data['status']}"
assert data["error_message"] == "sensor_read_failed"
print(f"    ✅ 失败路径正确")

# ---- 测试 12: 重复完成应返回 404 ----
print("\n[12] 重复完成同一任务 (应返回 404)")
code, data = req("POST", f"/api/tasks/{request_id}/complete", imu_result)
print(f"    HTTP {code}: {data}")
assert code == 404, f"期望 404, 实际 {code}"
print(f"    ✅ 已完成的任务不可重复提交")

print("\n" + "=" * 60)
print("  🎉 全部 12 项测试通过！第二周后端接口工作正常！")
print("=" * 60)