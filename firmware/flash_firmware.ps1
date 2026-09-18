$env:IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"
$env:IDF_TOOLS_PATH = "D:\Espressif\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "$env:PATH;D:\Espressif\Espressif\tools\cmake\3.30.2\bin"

. "$env:IDF_PATH\export.ps1"

Write-Host "Flashing to COM4..."
idf.py -p COM4 flash
if ($LASTEXITCODE -eq 0) {
    Write-Host "FLASH SUCCESS" -ForegroundColor Green
} else {
    Write-Host "FLASH FAILED" -ForegroundColor Red
    exit $LASTEXITCODE
}