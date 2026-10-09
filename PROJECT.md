# ESP32-S3-EYE + QMA7981 IMU 课程项目

## 项目结构

```
zhou/
├── firmware/                    # ESP-IDF 固件工程
│   ├── main/
│   │   ├── main.c              # 主程序入口
│   │   ├── qma7981.c/h         # QMA7981 I2C 驱动
│   │   ├── wifi_conn.c/h       # WiFi 连接管理
│   │   ├── http_post.c/h       # HTTP POST 上传
│   │   ├── http_get.c/h        # HTTP GET 请求
│   │   ├── cmd_poll.c/h        # 远程命令轮询
│   │   ├── secrets_template.h  # 配置模板
│   │   └── CMakeLists.txt
│   ├── CMakeLists.txt
│   └── build_and_flash.ps1     # 编译烧录脚本
├── server/                      # VPS 后端
│   ├── main.py                 # FastAPI 应用
│   ├── database.py             # SQLite 数据库管理
│   ├── requirements.txt        # Python 依赖
│   └── static/
│       └── index.html          # Web 前端页面
└── PROJECT.md                   # 本文件
```

---

## 1. ESP-IDF 固件说明

### 硬件配置
- **开发板**: ESP32-S3-EYE
- **IMU**: QMA7981 (I2C)
  - SDA: GPIO4
  - SCL: GPIO5
  - I2C地址: 0x12
  - 通信频率: 400kHz

### 功能模块

#### 1.1 QMA7981 驱动 (`qma7981.c/h`)
- 初始化 I2C 总线 (ESP-IDF v5.x 新 API)
- 验证 Chip ID (0x90)
- 读取三轴加速度 (14-bit, ±2g)
- I2C 总线扫描功能
- **容错处理**: 读取失败时返回错误码，主循环输出 `read_error` 状态

#### 1.2 WiFi 连接 (`wifi_conn.c/h`)
- STA 模式连接指定 WiFi
- 自动重连 (最多5次)
- 提供连接状态查询

#### 1.3 HTTP 通信 (`http_post.c/h`, `http_get.c/h`)
- POST JSON 数据到 VPS 后端
- GET 请求获取远程命令
- 超时设置: 5秒

#### 1.4 主循环 (`main.c`)
- 周期 100ms (10Hz) 读取传感器
- 串口输出 JSON (含时间戳)
- HTTP POST 上传到后端
- 读取失败时输出错误状态，不模拟数据

#### 1.5 远程命令 (`cmd_poll.c/h`)
- 每3秒轮询待处理命令
- 支持 `collect_once` (采集一次)
- 支持 `set_reporting` (暂停/恢复周期上报)

### JSON 数据格式

**周期上报 / 采集一次**:
```json
{
  "device_id": "g02-s20251040108-esp32s3eye",
  "sensor": "qma7981",
  "ts_device": 12.345,
  "ax": 0.1234,
  "ay": -0.0567,
  "az": 0.9876,
  "unit": "g",
  "raw": [1234, -567, 8765],
  "status": "ok"
}
```

**读取失败时**:
```json
{
  "device_id": "g02-s20251040108-esp32s3eye",
  "sensor": "qma7981",
  "ts_device": 12.345,
  "ax": 0.0,
  "ay": 0.0,
  "az": 0.0,
  "unit": "g",
  "raw": [0, 0, 0],
  "status": "read_error"
}
```

---

## 2. VPS 后端说明

### 技术栈
- **框架**: FastAPI (Python)
- **数据库**: SQLite (WAL 模式)
- **服务器**: Uvicorn

### API 端点

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/ingest` | 接收传感器数据 |
| GET | `/api/data` | 查询最近记录 (支持 `device_id` 和 `limit` 参数) |
| POST | `/api/commands` | 创建远程采集命令 |
| GET | `/api/commands/{device_id}/pending` | 设备轮询待处理命令 |
| PUT | `/api/commands/{request_id}` | 更新命令状态 |
| GET | `/api/commands/{request_id}` | 查询命令状态 |
| GET | `/` | Web 展示页面 |

### 数据库表结构

**sensor_data**:
- `id` (INTEGER PRIMARY KEY)
- `device_id` (TEXT)
- `sensor` (TEXT)
- `ts_device` (REAL) - 板端时间戳
- `ts_server` (TEXT) - 服务器时间戳
- `ax, ay, az` (REAL) - 三轴加速度
- `raw_x, raw_y, raw_z` (INTEGER) - 原始值
- `status` (TEXT) - 状态

**commands**:
- `request_id` (TEXT UNIQUE)
- `device_id` (TEXT)
- `command_type` (TEXT)
- `status` (TEXT)
- `created_at, received_at, completed_at` (TEXT)
- `result_id` (INTEGER)

---

## 3. Web 前端说明

### 功能
- 实时显示最新 IMU 数值 (X/Y/Z 三轴)
- 显示原始 14-bit 值
- 历史记录表格 (最多50条)
- 设备状态指示 (在线/离线)
- 无数据时显示 "暂无数据"
- 自动刷新 (每5秒)
- 支持设备筛选
- 远程采集控制

### 页面元素
- **状态栏**: 连接状态、设备ID、最后更新时间
- **数据卡片**: X/Y/Z 三轴加速度值
- **原始值**: 14-bit 原始数据
- **历史记录表**: 时间、设备、三轴数值、状态

---

## 4. 部署 & 烧录步骤

### 4.1 VPS 后端部署

#### 环境要求
- Python 3.10+
- pip

#### 步骤

```bash
# 1. 进入 server 目录
cd server

# 2. 创建虚拟环境 (可选)
python -m venv venv
# Windows:
venv\Scripts\activate
# Linux/Mac:
source venv/bin/activate

# 3. 安装依赖
pip install -r requirements.txt

# 4. 启动服务 (开发环境)
uvicorn main:app --host 0.0.0.0 --port 8000 --reload

# 5. 生产环境 (后台运行)
nohup uvicorn main:app --host 0.0.0.0 --port 8000 > server.log 2>&1 &
```

#### 验证后端
```bash
# 测试 API
curl http://localhost:8000/api/data
# 应返回: {"code": 0, "data": []}

# 访问 Web 页面
# 浏览器打开: http://<VPS_IP>:8000
```

### 4.2 ESP-IDF 固件烧录

#### 环境要求
- ESP-IDF v5.x (推荐 v5.2+)
- Python 3.8+
- USB 数据线

#### 步骤

```powershell
# 1. 进入 firmware 目录
cd firmware

# 2. 配置 secrets.h
# 复制模板并编辑 WiFi 凭据和服务器地址
Copy-Item main\secrets_template.h main\secrets.h
# 编辑 main\secrets.h:
#   - WIFI_SSID: 你的 WiFi 名称
#   - WIFI_PASS: 你的 WiFi 密码
#   - DEVICE_ID: 设备标识 (如 "g02-s20251040108-esp32s3eye")
#   - SERVER_BASE_URL: VPS 后端地址 (如 "http://10.1.41.154:8000")

# 3. 设置 ESP-IDF 环境
# Windows (PowerShell):
$env:IDF_PATH = "C:\espressif\frameworks\esp-idf-v5.x"
. $env:IDF_PATH\export.ps1

# Linux/Mac:
source $HOME/esp/esp-idf/export.sh

# 4. 编译项目
idf.py set-target esp32s3
idf.py build

# 5. 烧录固件 (ESP32-S3-EYE 连接 USB)
# Windows:
idf.py -p COM3 flash monitor
# Linux:
idf.py -p /dev/ttyUSB0 flash monitor

# 或使用提供的脚本 (Windows PowerShell)
.\build_and_flash.ps1
```

#### 使用 PowerShell 脚本

```powershell
# 编译
.\build_firmware.ps1

# 烧录 (指定 COM 口)
.\flash_firmware.ps1 COM3

# 监控串口
.\monitor_firmware.ps1 COM3

# 一键编译+烧录
.\build_and_flash.ps1 COM3
```

---

## 5. 联调验证步骤

### 5.1 硬件连接检查

1. **确认 QMA7981 连接**:
   - SDA → GPIO4
   - SCL → GPIO5
   - VCC → 3.3V
   - GND → GND

2. **上电后查看串口日志**:
   ```
   I qma7981: I2C master init (SDA=GPIO4, SCL=GPIO5, 400000 Hz)
   I qma7981: QMA7981 initialized (active mode)
   I qma7981: === I2C bus scan ===
   I qma7981:   I2C device found at 0x12
   I qma7981: scan complete, 1 device(s) found
   I qma7981: QMA7981 CHIP_ID = 0x90 (matched)
   ```

### 5.2 WiFi 连接验证

串口应输出:
```
I wifi: connecting to WiFi SSID: your_wifi_ssid
I wifi: got ip: 192.168.1.100
I main: WiFi connected
```

### 5.3 传感器数据验证

串口应周期输出 JSON (10Hz):
```json
{"device_id":"g02-s20251040108-esp32s3eye","sensor":"qma7981","ts_device":12.345,"ax":0.1234,"ay":-0.0567,"az":0.9876,"unit":"g","raw":[1234,-567,8765],"status":"ok"}
```

**验证点**:
- `status` 字段为 `"ok"`
- `ax, ay, az` 值在 ±2g 范围内
- 静止时 Z 轴约等于 1g (重力加速度)
- 晃动开发板时数值应有明显变化

### 5.4 后端接收验证

#### 方法1: 查看后端日志
```
INFO:     192.168.1.100:12345 - "POST /api/ingest HTTP/1.1" 200 OK
```

#### 方法2: 查询 API
```bash
# 查询最近10条数据
curl http://<VPS_IP>:8000/api/data?limit=10

# 查询指定设备
curl "http://<VPS_IP>:8000/api/data?device_id=g02-s20251040108-esp32s3eye"
```

#### 方法3: 直接查看 SQLite 数据库
```bash
# Linux/Mac
sqlite3 server/sensor_data.db "SELECT * FROM sensor_data ORDER BY id DESC LIMIT 5;"

# Windows (需安装 sqlite3)
sqlite3 server\sensor_data.db "SELECT * FROM sensor_data ORDER BY id DESC LIMIT 5;"
```

### 5.5 Web 页面验证

1. **打开页面**: `http://<VPS_IP>:8000`

2. **检查项**:
   - [ ] 状态栏显示设备在线 (绿色圆点)
   - [ ] 显示正确的设备ID
   - [ ] X/Y/Z 三轴数值正常显示 (非 "--")
   - [ ] 原始值显示 (非 "--")
   - [ ] 历史记录表格有数据
   - [ ] 数值随开发板晃动而变化

3. **无数据测试**:
   - 断开开发板电源
   - 页面应保留最后一条数据
   - 不应显示硬编码的数值
   - 状态栏应显示最后更新时间

4. **清空数据测试**:
   ```bash
   # 删除数据库文件
   rm server/sensor_data.db
   # 重启后端
   uvicorn main:app --host 0.0.0.0 --port 8000 --reload
   ```
   - 页面应显示 "暂无数据，等待板端上传..."
   - 数值显示为 "--"
   - 状态栏显示 "暂无数据"

### 5.6 容错测试

#### I2C 读取失败
- 断开 QMA7981 的 SDA 或 SCL 线
- 串口应输出 `status: "read_error"`
- 程序不应崩溃，继续尝试读取

#### WiFi 断开
- 关闭路由器或断开 WiFi
- 程序应继续运行，跳过 HTTP 上传
- 重新连接 WiFi 后应恢复正常上传

#### 后端不可达
- 停止 VPS 后端服务
- 串口应输出上传失败警告
- 程序不应崩溃，继续读取传感器

### 5.7 远程命令测试

#### 采集一次
```bash
curl -X POST http://<VPS_IP>:8000/api/commands \
  -H "Content-Type: application/json" \
  -d '{"device_id": "g02-s20251040108-esp32s3eye", "command_type": "collect_once"}'
```

串口应输出带 `request_id` 的 JSON:
```json
{"device_id":"...","sensor":"qma7981","ts_device":12.345,"ax":0.1234,"ay":-0.0567,"az":0.9876,"unit":"g","raw":[1234,-567,8765],"status":"collect_once","request_id":"..."}
```

#### 暂停/恢复周期上报
```bash
# 暂停
curl -X POST http://<VPS_IP>:8000/api/commands \
  -H "Content-Type: application/json" \
  -d '{"device_id": "g02-s20251040108-esp32s3eye", "command_type": "set_reporting", "params": "{\"enabled\": false}"}'

# 恢复
curl -X POST http://<VPS_IP>:8000/api/commands \
  -H "Content-Type: application/json" \
  -d '{"device_id": "g02-s20251040108-esp32s3eye", "command_type": "set_reporting", "params": "{\"enabled\": true}"}'
```

暂停后串口应停止输出周期 JSON，但程序继续运行。

---

## 6. 常见问题

### Q1: I2C 扫描找不到设备
- 检查接线 (SDA/SCL 是否接反)
- 检查上拉电阻 (QMA7981 模块可能已内置)
- 确认 I2C 地址正确 (0x12)

### Q2: Chip ID 读取失败
- 确认 QMA7981 已供电 (3.3V)
- 检查 I2C 频率 (尝试降低到 100kHz)
- 确认 GPIO 引脚配置正确

### Q3: WiFi 连接失败
- 确认 SSID 和密码正确
- 确认 WiFi 频段 (ESP32-S3 仅支持 2.4GHz)
- 检查路由器设置 (是否开启 MAC 过滤)

### Q4: HTTP POST 失败
- 确认 `SERVER_BASE_URL` 格式正确 (包含 `http://`)
- 确认 VPS 后端正在运行
- 检查防火墙设置 (端口 8000 是否开放)
- 确认开发板和 VPS 在同一网络或可路由

### Q5: Web 页面无数据
- 检查浏览器控制台 (F12) 是否有 JS 错误
- 确认后端 API 可访问 (`curl http://<VPS_IP>:8000/api/data`)
- 确认数据库中有数据 (`SELECT COUNT(*) FROM sensor_data;`)

---

## 7. 技术细节

### QMA7981 数据格式
- 14-bit 分辨率，小端序
- LSB [1:0] 为 NEW_DATA 状态位
- 默认量程 ±2g
- 换算公式: `accel_g = raw * 2.0 / 8191`

### 时间戳
- `ts_device`: 板端时间戳 (秒，自 ESP32 启动以来)
- `ts_server`: 服务器时间戳 (ISO 8601 格式)

### 数据库优化
- 使用 WAL 模式提高并发性能
- 索引: `(device_id, ts_device DESC)`

---

## 8. 许可证

本课程项目仅用于教学目的。
