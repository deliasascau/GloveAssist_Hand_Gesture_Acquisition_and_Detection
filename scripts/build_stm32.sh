#!/bin/bash
# Compilare firmware STM32 Blue Pill
set -e

west init -l .
west update --narrow -o=--depth=1
west build -p always -b stm32_min_dev@blue application/stm32_app --build-dir build-stm32

echo "STM32 build OK — $(du -h build-stm32/zephyr/zephyr.bin | cut -f1)"
