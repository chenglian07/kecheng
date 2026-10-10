@echo off
setlocal enabledelayedexpansion

set IDF_PATH=D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4
set IDF_PYTHON_ENV_PATH=D:\Espressif\Espressif\python_env\idf5.4_py3.11_env
set XTENSA_BIN=D:\Espressif\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin
set CMAKE_BIN=D:\Espressif\Espressif\tools\cmake\3.30.2\bin
set NINJA_BIN=D:\Espressif\Espressif\tools\ninja\1.12.1
set OPENOCD_BIN=D:\Espressif\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\bin
set ESPROM_BIN=D:\Espressif\Espressif\tools\esp-rom-elfs\20230320

set PATH=%XTENSA_BIN%;%CMAKE_BIN%;%NINJA_BIN%;%OPENOCD_BIN%;%ESPROM_BIN%;%PATH%
set IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe

echo ESP-IDF Environment Loaded
echo IDF_PATH: %IDF_PATH%

cd /d d:\shangke\zhou\firmware

%IDF_PYTHON% "%IDF_PATH%\tools\idf.py" %*

endlocal
