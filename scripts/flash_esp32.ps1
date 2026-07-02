# Build + Flash firmware ESP32 via esptool
# Rulat de self-hosted runner Windows cu ESP32 conectat USB

$env:ZEPHYR_BASE = 'C:\zephyr-workspace\zephyr'
$env:PATH = 'C:\zephyr-workspace\.venv\Scripts;' + $env:PATH

$python = 'C:\zephyr-workspace\.venv\Scripts\python.exe'
$BuildDir = 'C:\zephyr-workspace\build-glove-esp32'
$Board = 'esp32_devkitc_wroom/esp32/procpu'

if (Test-Path 'C:\zephyr-workspace\zephyr\boards\espressif\esp32_devkitc\esp32_devkitc_procpu.yaml') {
    $Board = 'esp32_devkitc/esp32/procpu'
}

Write-Host "Building ESP32..."
& $python -m west build -p always -b $Board application/esp32_app --build-dir $BuildDir

Write-Host "Flashing ESP32..."
& $python -m west flash --build-dir $BuildDir
Write-Host "ESP32 flash OK"
