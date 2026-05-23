# SPDX-License-Identifier: Apache-2.0
# GloveAssist — STM32F103 Application CMakeLists.txt

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gloveassist_stm32 LANGUAGES C)

# Shared + target-specific headers
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Common shared sources (protocol, CRC)
target_sources(app PRIVATE
    ../common/src/frame_protocol.c
)

# STM32-specific application sources
target_sources(app PRIVATE
    src/main.c
    src/sensor_logic.c
    src/safety_diag.c
    src/haptic_ui.c
    src/uart_comm.c
    src/security.c
    src/calibration.c
)
