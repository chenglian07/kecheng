@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
echo ========================================
echo  Week1 ESP32-S3-EYE 固件烧录脚本
echo ========================================
echo.

REM 检查 ESP-IDF 环境
if not defined IDF_PATH (
    echo [错误] ESP-IDF 环境未设置！
    echo 请先运行 ESP-IDF 的 export.bat 或打开 ESP-IDF 命令行
    pause
    exit /b 1
)

echo [1/5] 清理旧配置...
if exist sdkconfig (
    del sdkconfig
    echo   - 已删除旧 sdkconfig
) else (
    echo   - 无需清理
)

if exist build (
    echo   - 删除 build 目录...
    rmdir /s /q build
)

echo.
echo [2/5] 设置目标芯片 ESP32-S3...
call idf.py set-target esp32s3
if %errorlevel% neq 0 (
    echo [错误] 设置目标芯片失败！
    pause
    exit /b 1
)

echo.
echo [3/5] 编译固件...
call idf.py build
if %errorlevel% neq 0 (
    echo [错误] 编译失败！
    pause
    exit /b 1
)

echo.
echo ========================================
echo  准备烧录！
echo ========================================
echo.
echo 请确认：
echo   1. ESP32-S3-EYE 已通过 USB 连接到电脑
echo   2. 已安装 USB 驱动（CP210x 或 CH340）
echo.

REM 检测串口
echo 正在检测可用串口...
echo.

REM 尝试常见串口
set PORT=
for %%p in (COM1 COM2 COM3 COM4 COM5 COM6 COM7 COM8 COM9 COM10 COM11 COM12 COM13 COM14 COM15) do (
    mode %%p >nul 2>&1
    if not errorlevel 1 (
        echo   发现串口: %%p
        if "!PORT!"=="" set PORT=%%p
    )
)

if "%PORT%"=="" (
    echo.
    echo [警告] 未检测到串口！
    echo 请手动输入串口号（例如 COM3）:
    set /p PORT="串口: "
) else (
    echo.
    echo 将使用串口: %PORT%
    echo 如果不对，请中断并修改脚本中的 PORT 变量
)

echo.
echo [4/5] 烧录固件到 ESP32-S3-EYE...
call idf.py -p %PORT% flash
if %errorlevel% neq 0 (
    echo [错误] 烧录失败！
    echo 可能原因：
    echo   - 串口号错误
    echo   - USB 线只充电不传数据
    echo   - 需要按 BOOT 按钮进入下载模式
    pause
    exit /b 1
)

echo.
echo [5/5] 烧录完成！正在打开串口监视器...
echo.
echo 按 Ctrl+] 退出监视器
echo.
call idf.py -p %PORT% monitor

echo.
echo ========================================
echo  完成！
echo ========================================
pause
