#!/bin/bash
# Compilare firmware ESP32 DevKitC
set -e

west init -l .
west update --narrow -o=--depth=1
west build -p always -b esp32_devkitc_wroom/esp32/procpu application/esp32_app --build-dir build-esp32

echo "ESP32 build OK — $(du -h build-esp32/zephyr/zephyr.bin | cut -f1)"
