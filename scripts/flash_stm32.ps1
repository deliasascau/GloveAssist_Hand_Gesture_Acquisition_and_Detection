# Build + Flash firmware STM32 via ST-Link
# Rulat de self-hosted runner Windows cu ST-Link conectat USB

$env:ZEPHYR_BASE = 'C:\zephyr-workspace\zephyr'
$env:PATH = 'C:\zephyr-workspace\.venv\Scripts;' + $env:PATH

$python = 'C:\zephyr-workspace\.venv\Scripts\python.exe'
$BuildDir = 'C:\zephyr-workspace\build-glove-stm32'

Write-Host "Building STM32..."
& $python -m west build -p always -b stm32_min_dev@blue application/stm32_app --build-dir $BuildDir

Write-Host "Flashing STM32..."
& $python -m west flash --build-dir $BuildDir
Write-Host "STM32 flash OK"
