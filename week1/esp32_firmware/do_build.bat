@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Week1 ESP32-S3-EYE Build + Flash
echo ========================================

REM ====== ESP-IDF Environment ======
set IDF_PATH=D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4
set IDF_TOOLS_PATH=D:\Espressif\Espressif
set IDF_PYTHON_ENV_PATH=D:\Espressif\Espressif\python_env\idf5.4_py3.11_env
set XTENSA_BIN=D:\Espressif\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin
set CMAKE_BIN=D:\Espressif\Espressif\tools\cmake\3.30.2\bin
set NINJA_BIN=D:\Espressif\Espressif\tools\ninja\1.12.1
set OPENOCD_BIN=D:\Espressif\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\bin
set ESPROM_BIN=D:\Espressif\Espressif\tools\esp-rom-elfs\20230320
set IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe

set PATH=%XTENSA_BIN%;%CMAKE_BIN%;%NINJA_BIN%;%OPENOCD_BIN%;%ESPROM_BIN%;%PATH%

echo [ENV] IDF_PATH: %IDF_PATH%
echo [ENV] Python: %IDF_PYTHON%

REM ====== Check IDF exists ======
if not exist "%IDF_PATH%\tools\idf.py" (
    echo [ERROR] ESP-IDF not found at %IDF_PATH%
    exit /b 1
)
if not exist "%IDF_PYTHON%" (
    echo [ERROR] IDF Python not found at %IDF_PYTHON%
    exit /b 1
)
echo [ENV] OK

REM ====== Go to week1 firmware ======
cd /d d:\shangke\zhou\week1\esp32_firmware
echo [DIR] %CD%

REM ====== Clean ======
echo.
echo [STEP 1] Clean old config...
if exist sdkconfig del sdkconfig
if exist build rmdir /s /q build
echo   Done.

REM ====== Set target ======
echo.
echo [STEP 2] Set target esp32s3...
%IDF_PYTHON% "%IDF_PATH%\tools\idf.py" set-target esp32s3
if %errorlevel% neq 0 (
    echo [ERROR] set-target failed!
    exit /b 1
)

REM ====== Build ======
echo.
echo [STEP 3] Building firmware (this takes 2-3 minutes)...
%IDF_PYTHON% "%IDF_PATH%\tools\idf.py" build
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    exit /b 1
)

REM ====== Flash ======
echo.
echo [STEP 4] Flashing...
echo   Trying COM4 first (previously used), then COM5...

REM Try COM4
%IDF_PYTHON% "%IDF_PATH%\tools\idf.py" -p COM4 flash 2>nul
if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo  FLASH SUCCESS on COM4!
    echo ========================================
    echo.
    echo Opening monitor... (Ctrl+] to exit)
    %IDF_PYTHON% "%IDF_PATH%\tools\idf.py" -p COM4 monitor
    goto :end
)

REM Try COM5
echo   COM4 failed, trying COM5...
%IDF_PYTHON% "%IDF_PATH%\tools\idf.py" -p COM5 flash 2>nul
if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo  FLASH SUCCESS on COM5!
    echo ========================================
    echo.
    echo Opening monitor... (Ctrl+] to exit)
    %IDF_PYTHON% "%IDF_PATH%\tools\idf.py" -p COM5 monitor
    goto :end
)

REM Try COM3
echo   COM5 failed, trying COM3...
%IDF_PYTHON% "%IDF_PATH%\tools\idf.py" -p COM3 flash 2>nul
if %errorlevel% equ 0 (
    echo.
    echo ========================================
    echo  FLASH SUCCESS on COM3!
    echo ========================================
    echo.
    echo Opening monitor... (Ctrl+] to exit)
    %IDF_PYTHON% "%IDF_PATH%\tools\idf.py" -p COM3 monitor
    goto :end
)

echo.
echo ========================================
echo  FLASH FAILED on all ports!
echo ========================================
echo  Try:
echo    1. Check USB cable is connected
echo    2. Hold BOOT button, press RST, release BOOT
echo    3. Check COM port in Device Manager

:end
endlocal
