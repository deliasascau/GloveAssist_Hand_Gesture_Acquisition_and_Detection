cmake_minimum_required(VERSION 3.20.0)

# tf-psa-crypto probes host pthreads by default. This is an embedded Zephyr
# cross-build, so disable that host-only package lookup for quieter CMake.
set(CMAKE_DISABLE_FIND_PACKAGE_Threads TRUE CACHE BOOL "" FORCE)

# MQTT/WiFi stay enabled in prj.conf. Secrets are loaded only from this local
# ignored file when present, so credentials never need to live in git.
set(GLOVE_LOCAL_CREDENTIALS_CONF ${CMAKE_CURRENT_SOURCE_DIR}/credentials.conf)
if(EXISTS ${GLOVE_LOCAL_CREDENTIALS_CONF})
    if(DEFINED EXTRA_CONF_FILE)
        list(APPEND EXTRA_CONF_FILE ${GLOVE_LOCAL_CREDENTIALS_CONF})
    else()
        set(EXTRA_CONF_FILE ${GLOVE_LOCAL_CREDENTIALS_CONF})
    endif()
endif()

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

target_compile_definitions(app PRIVATE
    FRAME_SECURE_TX_ID=0x45U  # 'E' = ESP32
    FRAME_SECURE_RX_ID=0x53U  # 'S' = STM32
)

# Embed CA certificates for MQTT TLS (only when CONFIG_GLOVE_MQTT_TLS=y)
if(CONFIG_GLOVE_MQTT_TLS)
    set(CERT_ROOT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/certs/digicert_global_root_g2.pem)
    set(CERT_INTERMEDIATE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/certs/geotrust_tls_rsa_ca_g1.pem)
    if(NOT EXISTS ${CERT_ROOT_SRC} OR NOT EXISTS ${CERT_INTERMEDIATE_SRC})
        message(FATAL_ERROR
            "MQTT TLS enabled but Adafruit IO CA chain certs are missing.\n"
            "Expected files in application/esp32_app/certs/.")
    endif()
    generate_inc_file_for_target(app
        ${CERT_ROOT_SRC}
        ${ZEPHYR_BINARY_DIR}/include/generated/digicert_global_root_g2.pem.inc
    )
    generate_inc_file_for_target(app
        ${CERT_INTERMEDIATE_SRC}
        ${ZEPHYR_BINARY_DIR}/include/generated/geotrust_tls_rsa_ca_g1.pem.inc
    )
    target_include_directories(app PRIVATE
        ${ZEPHYR_BINARY_DIR}/include/generated
    )
endif()
