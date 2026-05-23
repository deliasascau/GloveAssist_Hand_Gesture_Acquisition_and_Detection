/**
 * @file sensor_logic.h
 * @brief STM32 Sensor Hub LITE (v2.0) API
 *
 * NEW ARCHITECTURE:
 * - STM32: ADC acquisition ONLY → sends RAW_ADC frames to ESP32 @ 50Hz
 * - ESP32: Filtering + Classification → sends GESTURE back
 * - STM32: Receives gesture → triggers haptic feedback
 */

#ifndef SENSOR_LOGIC_H
#define SENSOR_LOGIC_H

#include <stdint.h>

/**
 * @brief Sensor acquisition thread entry point (LITE version).
 *
 * Reads 4 flex sensors via ADC, sends RAW_ADC frames to ESP32 via UART.
 * No local filtering or classification.
 */
void sensor_thread_entry(void *p1, void *p2, void *p3);

/**
 * @brief Set current gesture (called from uart_comm when ESP32 sends GESTURE).
 * @param gesture_id Gesture ID from ESP32 (GESTURE_FIST, GESTURE_INDEX, etc.)
 */
void sensor_set_gesture(uint8_t gesture_id);

/**
 * @brief Get current gesture (received from ESP32).
 * @return Current gesture ID
 */
uint8_t sensor_get_current_gesture(void);

#endif /* SENSOR_LOGIC_H */
