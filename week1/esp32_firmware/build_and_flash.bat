@echo off
chcp 65001 >nul 2>&1
setlocal enabledelayedexpansion
echo ========================================
echo  Week1 全自动编译烧录
echo  WiFi: xiaomi_037C / 12345678
echo  服务器: http://10.1.41.154:8000
echo  设备ID: my-esp32  串口: COM4
echo ========================================

cd /d d:\shangke\zhou\week1\esp32_firmware

echo [0/3] 激活 ESP-IDF 环境...
call D:\Espressif\Espressif\idf_cmd_init.bat esp-idf-1f4dcd34b2e2a7b62ae397aa40f09a66
if errorlevel 1 (
    echo [FAIL] 环境激活失败
    exit /b 1
)
echo [OK] IDF_PATH=%IDF_PATH%

echo.
echo [1/3] 编译中（首次约 3-5 分钟）...
call idf.py build
if errorlevel 1 (
    echo [FAIL] 编译失败
    exit /b 1
)
echo [OK] 编译成功

echo.
echo [2/3] 烧录到 COM4...
call idf.py -p COM4 flash
if errorlevel 1 (
    echo [FAIL] 烧录失败
    exit /b 1
)
echo [OK] 烧录成功

echo.
echo [3/3] 启动串口监控（Ctrl+] 退出）...
call idf.py -p COM4 monitor

