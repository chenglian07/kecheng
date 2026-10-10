@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
echo ============================================
echo   EgoLink 局域网服务器 - Week 2
echo   ⚠️ 请右键「以管理员身份运行」
echo ============================================

for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /C:"IPv4"') do (
    for /f "tokens=1" %%b in ("%%a") do set MY_IP=%%b
)
echo [INFO] 本机 IP: %MY_IP%
echo [INFO] 浏览器: http://%MY_IP%:8000
echo [INFO] ESP32: http://%MY_IP%:8000
echo.

:: 检测网络类型
echo [CHECK] 网络类型检测...
for /f "tokens=*" %%a in ('powershell -Command "(Get-NetConnectionProfile | Where-Object {$_.IPv4Connectivity -eq 'Internet'}).NetworkCategory" 2^>nul') do (
    if "%%a"=="Public" (
        echo [WARN] 当前是「公用网络」！ESP32 连不上！
        echo [FIX]  设置 ^> 网络 ^> WiFi ^> 点击WiFi名 ^> 改为「专用网络」
        echo [FIX]  或者用管理员 PowerShell:
        echo        Set-NetConnectionProfile -Name "网络 7" -NetworkCategory Private
        echo.
    ) else (
        echo [OK]   专用网络 - 正常
    )
)

:: 防火墙放行 TCP 8000
echo [CHECK] 防火墙规则...
netsh advfirewall firewall show rule name="EgoLink-8000" >nul 2>&1
if !errorlevel!==0 (
    echo [OK]   TCP 8000 已放行
) else (
    echo [FIX]  添加防火墙规则...
    netsh advfirewall firewall add rule name="EgoLink-8000" dir=in action=allow protocol=TCP localport=8000 >nul 2>&1
    if !errorlevel!==0 (
        echo [OK]   已放行
    ) else (
        echo [FAIL] 需管理员权限！请右键此脚本「以管理员身份运行」
    )
)
echo.

cd /d d:\shangke\zhou\server

echo [START] 绑定 0.0.0.0:8000 ...
echo ============================================
python -m uvicorn main:app --host 0.0.0.0 --port 8000
