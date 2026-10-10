# ESP-IDF 环境设置脚本
$env:IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env"

# 工具链路径
$xtensa_bin = "D:\Espressif\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$cmake_bin = "D:\Espressif\Espressif\tools\cmake\3.30.2\bin"
$ninja_bin = "D:\Espressif\Espressif\tools\ninja\1.12.1"
$openocd_bin = "D:\Espressif\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\bin"
$esprom_bin = "D:\Espressif\Espressif\tools\esp-rom-elfs\20230320"

# 设置 PATH
$env:PATH = "$xtensa_bin;$cmake_bin;$ninja_bin;$openocd_bin;$esprom_bin;$env:PATH"

# Python 路径
$env:IDF_PYTHON = "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe"

Write-Host "ESP-IDF 环境已加载" -ForegroundColor Green
Write-Host "IDF_PATH: $env:IDF_PATH" -ForegroundColor Cyan

# 执行命令
if ($args.Count -gt 0) {
    $idfPy = Join-Path $env:IDF_PATH "tools\idf.py"
    & $env:IDF_PYTHON $idfPy $args
}
