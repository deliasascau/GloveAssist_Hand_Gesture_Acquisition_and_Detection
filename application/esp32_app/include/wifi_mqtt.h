/*
 * wifi_mqtt.h - WiFi + MQTT cloud publish for GloveAssist ESP32.
 *
 * Enabled in the default ESP32 build with CONFIG_WIFI and CONFIG_MQTT_LIB.
 *
 * MQTT topics (QoS 1, at least once):
 *   <user>/feeds/glove.gesture  <- gesture id (single digit ASCII, e.g. "6")
 *   <user>/feeds/glove.status   <- gesture name string (e.g. "WATER")
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
 * Non-blocking: WiFi/MQTT runs in a dedicated background thread.
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
 * @brief Publish gesture name string to glove.status feed.
 * @param status_str  Human-readable gesture name, e.g. "WATER", "HELP".
 * @return 0 on success, -ENOTCONN if not connected, -ENOTSUP if WiFi disabled.
 */
int  wifi_mqtt_publish_status(const char *status_str);

/**
 * @brief Drive MQTT keepalive and reconnect logic.
 *
 * Internal compatibility hook. The MQTT module starts its own Zephyr thread
 * from wifi_mqtt_init(), so the application main loop should not call this.
 */
void wifi_mqtt_process(void);

/** @return True when WiFi and MQTT broker are both connected. */
bool wifi_mqtt_is_connected(void);

/**
 * @brief Temporarily pause MQTT/TLS activity while BLE pairing/encryption runs.
 *
 * ESP32 RAM is tight with WiFi, TLS and BLE SMP active together. BLE calls this
 * before requesting link-layer encryption so MQTT avoids starting a new TLS
 * handshake while the BLE security procedure settles.
 */
void wifi_mqtt_pause_for_ble_security(uint32_t pause_ms);

/**
 * @brief Pause new MQTT/TLS connection attempts while BLE pairing runs.
 *
 * Call from the BLE connected callback. WiFi stays associated when possible;
 * wifi_mqtt_reconnect_after_ble_pairing() cancels the pause early after
 * security_changed or disconnected callbacks.
 */
void wifi_mqtt_disconnect_for_ble_pairing(void);

/**
 * @brief Allow MQTT/TLS connection attempts to resume after BLE pairing is
 * complete (success or failure). WiFi normally remains associated.
 */
void wifi_mqtt_reconnect_after_ble_pairing(void);

#endif /* WIFI_MQTT_H */
