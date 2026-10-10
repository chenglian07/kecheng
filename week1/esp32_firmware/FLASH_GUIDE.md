# ESP32-S3-EYE 烧录指南

## 📋 当前配置

- **WiFi 名称**: 431
- **WiFi 密码**: 88888888
- **服务器地址**: http://10.1.41.154:8000
- **设备ID**: g02-s20251040108-esp32s3eye
- **上传周期**: 5000ms (5秒)

---

## 🚀 快速烧录步骤

### 方法一：一键烧录（推荐）

1. **打开 ESP-IDF 命令行**
   - 在开始菜单搜索 "ESP-IDF Command Prompt"
   - 或者运行: `C:\Espressif\frameworks\esp-idf-v5.x\export.bat`

2. **进入固件目录**
   ```bash
   cd /d d:\shangke\zhou\week1\esp32_firmware
   ```

3. **运行烧录脚本**
   ```bash
   build_and_flash.bat
   ```

4. **按提示操作**
   - 脚本会自动检测串口
   - 如果检测不到，手动输入串口号（如 COM3）
   - 等待编译和烧录完成
   - 自动打开串口监视器查看日志

---

### 方法二：手动烧录

1. **打开 ESP-IDF 命令行**

2. **进入目录**
   ```bash
   cd /d d:\shangke\zhou\week1\esp32_firmware
   ```

3. **清理旧配置**
   ```bash
   if exist sdkconfig del sdkconfig
   if exist build rmdir /s /q build
   ```

4. **设置目标芯片**
   ```bash
   idf.py set-target esp32s3
   ```

5. **编译**
   ```bash
   idf.py build
   ```

6. **查看串口号**
   - 打开设备管理器
   - 找到 "端口 (COM 和 LPT)"
   - 记下 ESP32 的串口号（如 COM3）

7. **烧录**（将 COM3 替换为你的串口号）
   ```bash
   idf.py -p COM3 flash
   ```

8. **查看日志**
   ```bash
   idf.py -p COM3 monitor
   ```

---

## 🔍 查看串口号

### Windows
1. 按 `Win + X` → 设备管理器
2. 展开 "端口 (COM 和 LPT)"
3. 找到 "USB-SERIAL CH340" 或 "CP210x USB to UART"
4. 记下 COM 号

### 或者用命令
```bash
mode
```

---

## ⚠️ 常见问题

### Q1: 烧录失败，提示 "Failed to connect"
**解决方法**:
- 按住 ESP32-S3-EYE 上的 **BOOT** 按钮
- 按一下 **RST** 按钮
- 松开 **BOOT** 按钮
- 重新烧录

### Q2: 找不到串口
**解决方法**:
- 检查 USB 线是否是数据线（不是纯充电线）
- 安装 USB 驱动：
  - CH340: http://www.wch.cn/downloads/CH341SER_EXE.html
  - CP210x: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

### Q3: WiFi 连接失败
**解决方法**:
- 检查 WiFi 名称和密码是否正确
- 确认 WiFi 是 2.4GHz（ESP32 不支持 5GHz）
- 在串口日志中查看错误信息

### Q4: 上传数据失败
**解决方法**:
- 确认后端服务器已启动：
  ```bash
  cd d:\shangke\zhou\week1\vps_backend
  python main.py
  ```
- 检查服务器地址是否正确（应该是电脑的局域网 IP）
- 在浏览器访问 http://10.1.41.154:8000 确认服务正常

---

## 📊 正常启动日志

烧录完成后，串口监视器应该显示：

```
I (xxx) MAIN: ========================================
I (xxx) MAIN:   第1周 IMU 采集系统启动
I (xxx) MAIN:   设备ID: g02-s20251040108-esp32s3eye
I (xxx) MAIN:   目标服务器: http://10.1.41.154:8000
I (xxx) MAIN:   上传周期: 5000 ms
I (xxx) MAIN: ========================================
I (xxx) QMA7981: ✅ Chip ID 验证通过: 0xB3
I (xxx) WIFI: ✅ WiFi 已连接，IP: 10.1.41.xxx
I (xxx) MAIN: ✅ SNTP 时间已同步
I (xxx) MAIN: [#1] raw_x=45, raw_y=-112, raw_z=8120 | ax=43.9 mg, ay=-109.4 mg, az=7929.7 mg
I (xxx) HTTP_UPLOAD: ✅ 上传成功 (HTTP 200)
```

如果看到这些日志，说明一切正常！🎉

---

## 🌐 查看网页

烧录成功后，打开浏览器访问：
```
http://10.1.41.154:8000/
```

你应该能看到实时数据更新！

---

## 📞 需要帮助？

如果遇到问题，请提供：
1. 串口监视器的完整日志
2. 错误信息截图
3. 你的操作系统版本
