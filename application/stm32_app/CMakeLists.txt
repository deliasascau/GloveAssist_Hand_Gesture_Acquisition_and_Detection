cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(glove_stm32)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

target_sources(app PRIVATE
    src/main.c
    src/adc_sensor.c
    src/haptic.c
    src/uart_esp32.c
    src/alert.c
    ../common/src/frame_protocol.c
)
