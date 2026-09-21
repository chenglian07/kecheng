# ESP32-S3-EYE 一键编译 + 烧录脚本
# 用法：在 VS Code 终端中运行  .\build_and_flash.ps1
# 需要先安装 ESP-IDF v5.4.4

$IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"

Write-Host "=== 1. 加载 ESP-IDF 环境 ===" -ForegroundColor Cyan
. "$IDF_PATH\export.ps1"

Write-Host "`n=== 2. 编译固件 ===" -ForegroundColor Cyan
Set-Location "D:\shangke\zhou\firmware"
idf.py build

if ($LASTEXITCODE -ne 0) {
    Write-Host "编译失败，请检查错误信息" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "`n=== 3. 烧录到 COM4 ===" -ForegroundColor Cyan
idf.py -p COM4 flash

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n=== ✅ 烧录成功！===" -ForegroundColor Green
    Write-Host "运行以下命令查看串口输出：" -ForegroundColor Yellow
    Write-Host "  idf.py -p COM4 monitor" -ForegroundColor Yellow
} else {
    Write-Host "烧录失败" -ForegroundColor Red
}