$env:IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"
$env:IDF_TOOLS_PATH = "D:\Espressif\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "$env:PATH;D:\Espressif\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\Espressif\tools\ninja\1.12.1"

# Load ESP-IDF export
. "$env:IDF_PATH\export.ps1"

# Build
idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "BUILD SUCCESS" -ForegroundColor Green