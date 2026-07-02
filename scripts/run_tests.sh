#!/bin/bash
# Compilare și rulare unit tests pe native_sim (fără hardware)
set -e

west init -l .
west update --narrow -o=--depth=1
west build -p always -b native_sim/native/64 tests --build-dir build-tests

echo "Rulare teste..."
./build-tests/zephyr/zephyr.exe
