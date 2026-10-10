@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
echo ============================================
echo   EgoLink 局域网配置工具
echo   ⚠️ 请右键「以管理员身份运行」
echo ============================================
echo.

:: 1. 检测网络类型
echo [1/3] 检测网络类型...
for /f "tokens=*" %%a in ('powershell -Command "(Get-NetConnectionProfile -Name (Get-NetAdapter ^| Where-Object {$_.Status -eq 'Up'} ^| Select-Object -First 1).InterfaceAlias).NetworkCategory" 2^>nul') do set NET_TYPE=%%a
if "%NET_TYPE%"=="Public" (
    echo [WARN] 当前网络: 公用网络 ^(Public^) - ESP32 无法连接！
    echo [FIX]  正在改为专用网络...
    powershell -Command "Set-NetConnectionProfile -Name (Get-NetAdapter ^| Where-Object {$_.Status -eq 'Up'} ^| Select-Object -First 1).InterfaceAlias -NetworkCategory Private" 2>nul
    if !errorlevel!==0 (
        echo [OK]   已改为专用网络
    ) else (
        echo [FAIL] 修改失败，请手动操作: 设置^>网络^>WiFi^>点击WiFi名^>改为专用
    )
) else (
    echo [OK]   网络类型: 专用网络 ^(Private^) - 正常
)
echo.

:: 2. 添加防火墙规则
echo [2/3] 添加防火墙入站规则 TCP 8000...
netsh advfirewall firewall show rule name="EgoLink-8000" >nul 2>&1
if %errorlevel%==0 (
    echo [OK]   规则已存在，跳过
) else (
    netsh advfirewall firewall add rule name="EgoLink-8000" dir=in action=allow protocol=TCP localport=8000
    if %errorlevel%==0 (
        echo [OK]   防火墙已放行 TCP 8000
    ) else (
        echo [FAIL] 添加失败，请以管理员身份运行此脚本
    )
)
echo.

:: 3. 显示本机 IP
echo [3/3] 本机局域网 IP:
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /C:"IPv4"') do (
    for /f "tokens=1" %%b in ("%%a") do echo        %%b
)
echo.
echo ============================================
echo  配置完成！ESP32 的 SERVER_URL 应填上面的 IP
echo  示例: http://10.1.41.154:8000
echo ============================================
pause
