@echo off
set IDF_PATH=D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4
set IDF_TOOLS_PATH=D:\Espressif\Espressif
set IDF_PYTHON_ENV_PATH=D:\Espressif\Espressif\python_env\idf5.4_py3.11_env
set XTENSA_BIN=D:\Espressif\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin
set CMAKE_BIN=D:\Espressif\Espressif\tools\cmake\3.30.2\bin
set NINJA_BIN=D:\Espressif\Espressif\tools\ninja\1.12.1
set OPENOCD_BIN=D:\Espressif\Espressif\tools\openocd-esp32\v0.12.0-esp32-20241016\openocd-esp32\bin
set ESPROM_BIN=D:\Espressif\Espressif\tools\esp-rom-elfs\20230320
set IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe
set ESP_TOOL=%IDF_PYTHON_ENV_PATH%\Scripts\esptool.exe
set PATH=%XTENSA_BIN%;%CMAKE_BIN%;%NINJA_BIN%;%OPENOCD_BIN%;%ESPROM_BIN%;%PATH%

echo === Direct esptool flash ===
%IDF_PYTHON% -m esptool --chip esp32s3 -p COM5 -b 460800 --before=default_reset --after=hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x0 d:\shangke\zhou\week1\esp32_firmware\build\bootloader\bootloader.bin 0x10000 d:\shangke\zhou\week1\esp32_firmware\build\week1_imu_collector.bin 0x8000 d:\shangke\zhou\week1\esp32_firmware\build\partition_table\partition-table.bin
echo FLASH_EXIT=%errorlevel%