/*
 * wifi_mqtt.h - WiFi + MQTT cloud publish for GloveAssist ESP32.
 *
 * Enabled only when CONFIG_WIFI and CONFIG_MQTT_LIB are set
 * (add prj_wifi_mqtt.conf via -DEXTRA_CONF_FILE).
 *
 * MQTT topics (QoS 0):
 *   gloveassist/gesture  <- gesture id (single digit ASCII, e.g. "6")
 *   gloveassist/adc      <- raw ADC values CSV (e.g. "1234,2100,890,3050")
 *
 * Credentials via Kconfig (see Kconfig in this directory):
 *   CONFIG_GLOVE_WIFI_SSID, CONFIG_GLOVE_WIFI_PASSWORD
 *   CONFIG_GLOVE_MQTT_BROKER_HOST, CONFIG_GLOVE_MQTT_BROKER_PORT
 */

#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <stdbool.h>
#include <stdint.h>
#include "frame_protocol.h"

/**
 * @brief Initialize WiFi and start MQTT client.
 *
 * Non-blocking: WiFi association runs in the background via net_mgmt events.
 * Poll wifi_mqtt_is_connected() to check readiness.
 *
 * @return 0 on success, negative errno on failure.
 */
int  wifi_mqtt_init(void);

/**
 * @brief Publish a gesture event to MQTT broker.
 * @param gesture  Gesture ID.
 * @return 0 on success, -ENOTCONN if not connected, -ENOTSUP if WiFi disabled.
 */
int  wifi_mqtt_publish_gesture(gesture_id_t gesture);

/**
 * @brief Publish raw ADC snapshot to MQTT broker.
 * @param adc  Four 12-bit ADC values [index, middle, ring, pinky].
 * @return 0 on success, -ENOTCONN if not connected, -ENOTSUP if WiFi disabled.
 */
int  wifi_mqtt_publish_adc(const uint16_t adc[4]);

/**
 * @brief Drive MQTT keepalive and reconnect logic.
 *
 * Call once per second from the main loop.
 * Processes MQTT_EVT_PINGRESP, reconnects after drop, etc.
 */
void wifi_mqtt_process(void);

/** @return True when WiFi and MQTT broker are both connected. */
bool wifi_mqtt_is_connected(void);

#endif /* WIFI_MQTT_H */
