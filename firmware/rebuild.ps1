$log = "D:\shangke\zhou\firmware\build_output.txt"
"Starting build at $(Get-Date)" | Out-File $log

$env:IDF_PATH = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4"
$env:IDF_TOOLS_PATH = "D:\Espressif\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env"
$env:PATH = "$env:PATH;D:\Espressif\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\Espressif\tools\ninja\1.12.1"
$python = "D:\Espressif\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe"
$idf_py = "D:\Espressif\Espressif\frameworks\esp-idf-v5.4.4\tools\idf.py"

Set-Location "D:\shangke\zhou\firmware"
& $python $idf_py -B build2 build 2>&1 >> $log
"EXIT_CODE=$LASTEXITCODE" | Out-File $log -Append