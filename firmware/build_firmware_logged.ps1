$env:IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"
$env:IDF_TOOLS_PATH = "D:\Espressif\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "$env:PATH;D:\Espressif\Espressif\tools\cmake\3.30.2\bin"

. "$env:IDF_PATH\export.ps1" 2>&1 | Out-Null

$logFile = "D:\shangke\zhou\firmware\build_output.log"
idf.py build 2>&1 | Tee-Object -FilePath $logFile
$exitCode = $LASTEXITCODE
Write-Host "EXIT_CODE=$exitCode"