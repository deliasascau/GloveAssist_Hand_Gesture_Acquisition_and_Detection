/**
 * @file main.c
 * @brief GloveAssist ESP32 firmware — BRAIN (v2.0 Architecture)
 *
 * ESP32 responsibilities (NEW ARCH):
 *   - UART2 RX: receive RAW ADC frames from STM32 (sensor hub)
 *   - Filtering: Apply moving average filter
 *   - Classification: Detect gestures from filtered data
 *   - UART2 TX: send GESTURE frames back to STM32 for haptic feedback
 *   - BLE NUS: forward gestures to phone, relay commands to STM32
 *   - WiFi/MQTT: publish gestures to Adafruit IO dashboard
 *   - Calibration: Handle all calibration logic, store thresholds in ESP32 NVS
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_BOOTLOADER_MCUBOOT) && defined(CONFIG_MCUBOOT_IMG_MANAGER)
#include <zephyr/dfu/mcuboot.h>
#endif

#include "app_config.h"
#include "common_types.h"
#include "frame_protocol.h"
#include "comms_ble.h"
#include "uart_comm.h"
#include "wifi_mqtt.h"
#include "sensor_filter.h"
#include "gesture_classify.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Thread definitions ---------- */
K_THREAD_DEFINE(uart_comm_tid, STACK_UART_COMM,
                uart_comm_thread_entry, NULL, NULL, NULL,
                PRIO_UART_COMM_THREAD, 0, 0);

#if defined(CONFIG_NETWORKING) && defined(CONFIG_MQTT_LIB)
K_THREAD_DEFINE(wifi_mqtt_tid, STACK_WIFI_MQTT,
                wifi_mqtt_thread_entry, NULL, NULL, NULL,
                PRIO_WIFI_MQTT_THREAD, 0, 0);
#endif

/* ---------- Message queue: UART RX gestures → BLE TX ---------- */
K_MSGQ_DEFINE(ble_tx_msgq, sizeof(sensor_packet_t), 8, 4);

int main(void)
{
    LOG_INF("=== GloveAssist ESP32 Brain (v2.0) ===");
#if defined(CONFIG_NETWORKING) && defined(CONFIG_MQTT_LIB)
    LOG_INF("Threads: uart_comm(P%d) wifi_mqtt(P%d)",
            PRIO_UART_COMM_THREAD, PRIO_WIFI_MQTT_THREAD);
#else
    LOG_INF("Threads: uart_comm(P%d), WiFi/MQTT disabled for BLE test",
            PRIO_UART_COMM_THREAD);
#endif

    /* Initialize processing modules (NEW v2.0) */
    int ret = sensor_filter_init();
    if (ret < 0) {
        LOG_ERR("sensor_filter_init failed: %d", ret);
    }

    ret = gesture_classify_init();
    if (ret < 0) {
        LOG_ERR("gesture_classify_init failed: %d", ret);
    }

    ret = ble_init();
    if (ret < 0) {
        LOG_ERR("BLE init failed: %d", ret);
    }

    /*
     * Confirm this image as valid so MCUboot does not revert to the
     * previous firmware on the next reboot (revert mode safety feature).
     * Must be called after all critical subsystems are up.
     * If the device resets before reaching this line, MCUboot will
     * automatically restore the previous known-good image.
     */
#if defined(CONFIG_BOOTLOADER_MCUBOOT) && defined(CONFIG_MCUBOOT_IMG_MANAGER)
    ret = boot_write_img_confirmed();
    if (ret < 0) {
        LOG_WRN("MCUboot image confirm failed: %d (first boot?)", ret);
    } else {
        LOG_INF("MCUboot image confirmed as valid");
    }
#endif

    LOG_INF("ESP32 Brain initialized — processing pipeline ready");
    LOG_INF("All threads auto-started — main() returning to idle");
    return 0;
}
