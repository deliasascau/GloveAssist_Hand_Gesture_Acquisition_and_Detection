cmake_minimum_required(VERSION 3.20.0)

# tf-psa-crypto probes host pthreads by default. This is an embedded Zephyr
# cross-build, so disable that host-only package lookup for quieter CMake.
set(CMAKE_DISABLE_FIND_PACKAGE_Threads TRUE CACHE BOOL "" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(glove_esp32)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

target_sources(app PRIVATE
    src/main.c
    src/gesture.c
    src/uart_stm32.c
    src/ble_adc.c
    src/gpio_alert.c
    src/calibration.c
    src/wifi_mqtt.c
    ../common/src/frame_protocol.c
)

# Embed CA certificate for MQTT TLS (only when CONFIG_GLOVE_MQTT_TLS=y)
if(CONFIG_GLOVE_MQTT_TLS)
    set(CERT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/certs/mosquitto_ca.pem)
    if(NOT EXISTS ${CERT_SRC})
        message(FATAL_ERROR
            "MQTT TLS enabled but cert not found: ${CERT_SRC}\n"
            "See application/esp32_app/certs/mosquitto_ca.pem for instructions.")
    endif()
    generate_inc_file_for_target(app
        ${CERT_SRC}
        ${ZEPHYR_BINARY_DIR}/include/generated/mosquitto_ca.pem.inc
    )
    target_include_directories(app PRIVATE
        ${ZEPHYR_BINARY_DIR}/include/generated
    )
endif()
