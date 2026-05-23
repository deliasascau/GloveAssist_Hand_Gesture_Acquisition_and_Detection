/**
 * @file wifi_mqtt.h
 * @brief WiFi connection + Adafruit IO MQTT publishing (ESP32)
 *
 * Architecture:
 *   wifi_mqtt_thread — connects to WiFi, then to MQTT broker,
 *                      publishes gesture/status from a msgq.
 *
 * Publishing from other threads:
 *   wifi_mqtt_publish_gesture(id, confidence) — non-blocking enqueue
 *   wifi_mqtt_publish_status(alive, ble_conn) — non-blocking enqueue
 */

#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief WiFi + MQTT background thread entry (K_THREAD_DEFINE).
 */
void wifi_mqtt_thread_entry(void *p1, void *p2, void *p3);

/**
 * @brief Enqueue a gesture event for MQTT publishing.
 *
 * @param gesture_id   Gesture identifier (GESTURE_* from app_config.h).
 * @param confidence   Detection confidence 0-100.
 * @return 0 on success, -EAGAIN if queue full (dropped).
 */
int wifi_mqtt_publish_gesture(uint8_t gesture_id, uint8_t confidence);

/**
 * @brief Enqueue a status update for MQTT publishing.
 *
 * @param esp32_alive  Always true when called from ESP32.
 * @param ble_conn     True if BLE central is connected.
 */
int wifi_mqtt_publish_status(bool esp32_alive, bool ble_conn);

/**
 * @brief Returns true if WiFi is connected and MQTT broker is reachable.
 */
bool wifi_mqtt_is_connected(void);

#endif /* WIFI_MQTT_H */
