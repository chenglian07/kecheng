@echo off
set IDF_PYTHON_ENV_PATH=D:\Espressif\Espressif\python_env\idf5.4_py3.11_env
set IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe

echo === Erasing and flashing REFERENCE firmware to COM5 ===
%IDF_PYTHON% -m esptool -p COM5 -b 460800 --before default_reset --after hard_reset --chip esp32s3 erase_flash
echo Erase done: %errorlevel%

%IDF_PYTHON% -m esptool -p COM5 -b 460800 --before default_reset --after hard_reset --chip esp32s3 write_flash --flash_mode dio --flash_freq 80m --flash_size 2MB 0x0 d:\shangke\zhou\firmware\build_run\bootloader\bootloader.bin 0x10000 d:\shangke\zhou\firmware\build_run\kecheng.bin 0x8000 d:\shangke\zhou\firmware\build_run\partition_table\partition-table.bin
echo EXIT_CODE=%errorlevel%