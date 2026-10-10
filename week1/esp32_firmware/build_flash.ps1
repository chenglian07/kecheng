# Week1 ESP32-S3-EYE 构建烧录脚本
# 自动设置 ESP-IDF 环境 → 清理 → 编译 → 烧录

$ErrorActionPreference = "Stop"

# ====== ESP-IDF 环境 ======
$env:IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"
$env:IDF_TOOLS_PATH = "D:\Espressif\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Week1 ESP32-S3-EYE 构建烧录" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 加载 ESP-IDF 环境
Write-Host "[1/5] 加载 ESP-IDF 环境..." -ForegroundColor Yellow
try {
    . "$env:IDF_PATH\export.ps1"
    Write-Host "  ESP-IDF 环境加载成功!" -ForegroundColor Green
} catch {
    Write-Host "  export.ps1 加载失败，手动设置 PATH..." -ForegroundColor Yellow
    $xtensa_bin = "D:\Espressif\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
    $cmake_bin = "D:\Espressif\Espressif\tools\cmake\3.30.2\bin"
    $ninja_bin = "D:\Espressif\Espressif\tools\ninja\1.12.1"
    $openocd_bin = "D:\Espressif\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\bin"
    $esprom_bin = "D:\Espressif\Espressif\tools\esp-rom-elfs\20230320"
    $env:PATH = "$xtensa_bin;$cmake_bin;$ninja_bin;$openocd_bin;$esprom_bin;$env:PATH"
    $env:IDF_PYTHON = "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe"
}

# ====== 进入 week1 固件目录 ======
$firmwareDir = "d:\shangke\zhou\week1\esp32_firmware"
Set-Location $firmwareDir
Write-Host ""
Write-Host "[2/5] 进入目录: $firmwareDir" -ForegroundColor Yellow

# ====== 清理旧配置 ======
Write-Host ""
Write-Host "[3/5] 清理旧配置..." -ForegroundColor Yellow
if (Test-Path "sdkconfig") {
    Remove-Item "sdkconfig" -Force
    Write-Host "  删除旧 sdkconfig" -ForegroundColor Gray
}
if (Test-Path "build") {
    Remove-Item "build" -Recurse -Force
    Write-Host "  删除旧 build 目录" -ForegroundColor Gray
}

# ====== 设置目标芯片 ======
Write-Host ""
Write-Host "[4/5] 设置目标芯片 ESP32-S3 并编译..." -ForegroundColor Yellow
idf.py set-target esp32s3
if ($LASTEXITCODE -ne 0) {
    Write-Host "设置目标芯片失败!" -ForegroundColor Red
    exit 1
}

idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "编译失败!" -ForegroundColor Red
    exit 1
}

# ====== 检测串口并烧录 ======
Write-Host ""
Write-Host "[5/5] 检测串口并烧录..." -ForegroundColor Yellow

# 尝试检测可用串口
$comPorts = @()
$modeOutput = & cmd /c "mode" 2>&1
foreach ($line in $modeOutput) {
    if ($line -match "COM(\d+)") {
        $comPorts += "COM$($Matches[1])"
    }
}
$comPorts = $comPorts | Select-Object -Unique
Write-Host "  可用串口: $($comPorts -join ', ')" -ForegroundColor Gray

# 优先尝试之前用过的 COM4，否则用最后一个非 COM1/COM2 的
$port = "COM4"
if ($comPorts -contains "COM5") { $port = "COM5" }
elseif ($comPorts -contains "COM4") { $port = "COM4" }
elseif ($comPorts -contains "COM3") { $port = "COM3" }
else {
    # 用最后一个（排除 COM1 COM2）
    $filtered = $comPorts | Where-Object { $_ -ne "COM1" -and $_ -ne "COM2" }
    if ($filtered.Count -gt 0) { $port = $filtered[-1] }
    else { $port = $comPorts[-1] }
}

Write-Host "  使用串口: $port" -ForegroundColor Cyan
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host " 开始烧录..." -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

idf.py -p $port flash
if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "✅ 烧录成功！正在打开串口监视器..." -ForegroundColor Green
    Write-Host "按 Ctrl+] 退出监视器" -ForegroundColor Gray
    Write-Host ""
    idf.py -p $port monitor
} else {
    Write-Host ""
    Write-Host "❌ 烧录失败！请检查:" -ForegroundColor Red
    Write-Host "  1. USB 线已连接" -ForegroundColor Yellow
    Write-Host "  2. 串口号正确 (当前: $port)" -ForegroundColor Yellow
    Write-Host "  3. 尝试按住 BOOT 键再按 RST 键进入下载模式" -ForegroundColor Yellow
}
