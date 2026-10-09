# 第1周 部署步骤

## 一、项目结构总览

```
week1/
├── esp32_firmware/          # ESP-IDF 工程
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults   # WiFi/服务器/设备ID 配置
│   └── main/
│       ├── CMakeLists.txt
│       ├── Kconfig.projbuild # menuconfig 自定义选项
│       ├── main.c            # 主程序
│       ├── qma7981.c/h       # QMA7981 I2C 驱动
│       ├── wifi_manager.c/h  # WiFi 连接
│       └── http_upload.c/h   # HTTP POST 上传
├── vps_backend/              # FastAPI 后端
│   ├── main.py
│   ├── database.py
│   ├── models.py
│   └── requirements.txt
├── web_frontend/
│   └── index.html            # 前端页面（单文件）
└── docs/
    ├── deployment.md          # 本文件
    └── observation_log.md     # 观测记录
```

---

## 二、VPS 后端部署

### 2.1 环境要求
- Python 3.10+
- 公网 IP（或内网测试用 localhost）

### 2.2 安装依赖
```bash
cd week1/vps_backend
pip install -r requirements.txt
```

### 2.3 启动服务
```bash
# 开发模式（热重载）
uvicorn main:app --host 0.0.0.0 --port 8000 --reload

# 生产模式
uvicorn main:app --host 0.0.0.0 --port 8000 --workers 2
```

### 2.4 验证后端
```bash
# 健康检查
curl http://YOUR_VPS_IP:8000/api/health

# 手动测试上传
curl -X POST http://YOUR_VPS_IP:8000/api/imu \
  -H "Content-Type: application/json" \
  -d '{"device_id":"test_01","timestamp":1700000000,"ax_mg":12.5,"ay_mg":-34.2,"az_mg":987.6,"raw_x":102,"raw_y":-280,"raw_z":8081,"chip_id":179}'

# 查询数据
curl http://YOUR_VPS_IP:8000/api/imu

# 查询设备列表
curl http://YOUR_VPS_IP:8000/api/devices
```

### 2.5 访问前端页面
浏览器打开：`http://YOUR_VPS_IP:8000/`

前端会自动请求后端 API 获取数据并展示。

---

## 三、ESP32 板端部署

### 3.1 环境要求
- ESP-IDF v5.1+ （推荐 v5.2）
- USB 数据线连接 ESP32-S3-EYE

### 3.2 配置 WiFi 和服务器地址

**方式一：修改 sdkconfig.defaults**（推荐首次）

编辑 `esp32_firmware/sdkconfig.defaults`：
```
CONFIG_ESP_WIFI_SSID="你的WiFi名称"
CONFIG_ESP_WIFI_PASSWORD="你的WiFi密码"
CONFIG_SERVER_URL="http://你的VPS公网IP:8000"
CONFIG_DEVICE_ID="group_01"
CONFIG_UPLOAD_INTERVAL_MS=5000
```

**方式二：menuconfig 图形界面**
```bash
cd esp32_firmware
idf.py set-target esp32s3
idf.py menuconfig
```
在 `Week1 IMU Collector Configuration` 菜单中修改各项。

### 3.3 编译烧录
```bash
cd week1/esp32_firmware

# 设置目标芯片
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录（自动检测串口号，或指定 -p COMx）
idf.py -p COM3 flash

# 查看串口日志
idf.py -p COM3 monitor
```

### 3.4 硬件接线确认

| 信号 | GPIO | 说明 |
|------|------|------|
| I2C SDA | GPIO4 | QMA7981 SDA |
| I2C SCL | GPIO5 | QMA7981 SCL |
| I2C 地址 | 0x12 | QMA7981 固定地址 |
| SD CMD | GPIO38 | SD 卡（本周不用） |
| SD CLK | GPIO39 | SD 卡（本周不用） |
| SD D0  | GPIO40 | SD 卡（本周不用） |

### 3.5 串口日志验证

正常启动后，串口应打印：
```
I (xxx) MAIN: ========================================
I (xxx) MAIN:   第1周 IMU 采集系统启动
I (xxx) MAIN:   设备ID: group_01
I (xxx) MAIN:   目标服务器: http://1.2.3.4:8000
I (xxx) MAIN:   上传周期: 5000 ms
I (xxx) MAIN: ========================================
I (xxx) QMA7981: ✅ Chip ID 验证通过: 0xB3
I (xxx) QMA7981: ✅ QMA7981 初始化完成
I (xxx) MAIN: ✅ 测试读取成功: raw_x=xxx, raw_y=xxx, raw_z=xxx
I (xxx) WIFI: ✅ WiFi 已连接，IP: 192.168.x.x
I (xxx) MAIN: ✅ SNTP 时间已同步: 2024-xx-xx xx:xx:xx
I (xxx) MAIN: [#1] raw_x=102, raw_y=-280, raw_z=8081 | ax=99.7 mg, ay=-273.4 mg, az=7891.6 mg | ts=1700000000
I (xxx) HTTP_UPLOAD: ✅ 上传成功 (HTTP 200)
```

---

## 四、联调验证步骤

### 4.1 三者一致性核对

1. **板端串口**：记录一条日志，如：
   ```
   [#5] raw_x=102, raw_y=-280, raw_z=8081 | ax=99.7 mg, ay=-273.4 mg, az=7891.6 mg | ts=1700000050
   ```

2. **VPS 数据库**：
   ```bash
   sqlite3 week1/vps_backend/imu_data.db "SELECT * FROM imu_data ORDER BY id DESC LIMIT 1;"
   ```
   核对 raw_x/raw_y/raw_z、ax/ay/az、timestamp、device_id 完全一致。

3. **Web 页面**：刷新页面，核对最新卡片显示的值与上面两者一致。

### 4.2 停止采集验证
- 拔掉 ESP32 电源 / 按 RST 按钮
- 观察 Web 页面：30 秒后出现黄色警告 "数据未更新"，保留旧时间戳
- 页面不会清空旧数据

### 4.3 设备 ID 区分
- 修改 `CONFIG_DEVICE_ID` 为不同值，烧录多个设备
- Web 页面可通过下拉框筛选不同设备

---

## 五、备用方案

### 5.1 VPS 故障 → 本地测试
在本地运行后端（与 VPS 操作完全一致）：
```bash
cd week1/vps_backend
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```
ESP32 的 `CONFIG_SERVER_URL` 改为本地 IP：
```
CONFIG_SERVER_URL="http://192.168.1.100:8000"
```

### 5.2 WiFi 不可用
- ESP32 会反复重启尝试连接
- 串口会打印 WiFi 连接失败日志
- USB 串口仅用于调试查看日志，**不能**替代 WiFi 联网上传

### 5.3 传感器读取失败
- 串口打印 `❌ QMA7981 初始化失败` → 检查 I2C 接线
- 确认 SDA=GPIO4, SCL=GPIO5
- 用万用表量 I2C 引脚是否有上拉（3.3V）
- Chip ID 不是 0xB3 → 可能传感器型号不对或地址不对