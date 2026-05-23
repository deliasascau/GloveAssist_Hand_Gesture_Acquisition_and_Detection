# SPDX-License-Identifier: Apache-2.0
# GloveAssist — ESP32 Application CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gloveassist_esp32 LANGUAGES C)

# Shared + target-specific headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Common shared sources (protocol, CRC)
target_sources(app PRIVATE
    ../common/src/frame_protocol.c
)

# ESP32-specific application sources
target_sources(app PRIVATE
    src/main.c
    src/uart_comm.c
    src/comms_ble.c
    src/sensor_filter.c
    src/gesture_classify.c
)

if(CONFIG_NETWORKING AND CONFIG_MQTT_LIB)
    target_sources(app PRIVATE src/wifi_mqtt.c)
endif()
