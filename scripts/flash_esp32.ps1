# Build + Flash firmware ESP32 via esptool
# Rulat de self-hosted runner Windows cu ESP32 conectat USB

$env:ZEPHYR_BASE = 'C:\zephyr-workspace\zephyr'
$env:PATH = 'C:\zephyr-workspace\.venv\Scripts;' + $env:PATH

$python = 'C:\zephyr-workspace\.venv\Scripts\python.exe'
$BuildDir = 'C:\zephyr-workspace\build-glove-esp32'

Write-Host "Building ESP32..."
& $python -m west build -p always -b esp32_devkitc/esp32/procpu application/esp32_app --build-dir $BuildDir

Write-Host "Flashing ESP32..."
& $python -m west flash --build-dir $BuildDir
Write-Host "ESP32 flash OK"
