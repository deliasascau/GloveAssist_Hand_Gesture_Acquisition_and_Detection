/**
 * @file comms_ble.h
 * @brief BLE NUS (Nordic UART Service) gateway API (ESP32)
 */

#ifndef COMMS_BLE_H
#define COMMS_BLE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialise and start BLE advertising (NUS service).
 * @return 0 on success, negative Zephyr error code on failure.
 */
int ble_init(void);

/**
 * @brief Send a GATT notification to the connected BLE central.
 *
 * @param data  Payload buffer.
 * @param len   Number of bytes to send.
 * @return 0 on success, negative on error or if no central is connected.
 */
int ble_send_notification(const uint8_t *data, uint16_t len);

/**
 * @brief Check if a BLE central is currently connected.
 * @return true if connected.
 */
bool ble_is_connected(void);

/**
 * @brief Get last RSSI reading (dBm, 0 if not available).
 * @return RSSI as signed byte magnitude stored in uint8_t.
 */
uint8_t ble_get_rssi(void);

/**
 * @brief Format 4 raw 12-bit ADC values as text and send via BLE NUS notify.
 *
 * Sends a string like: "I:1234 M:2048 R:3012 P:0512\n"
 * Use nRF Connect or Serial Bluetooth Terminal on the phone to read values.
 *
 * @param raw  Array of 4 uint16_t: [index, middle, ring, pinky]
 * @return 0 on success, negative if not connected or notify disabled.
 */
int ble_send_raw_adc_text(const uint16_t raw[4]);

#endif /* COMMS_BLE_H */
