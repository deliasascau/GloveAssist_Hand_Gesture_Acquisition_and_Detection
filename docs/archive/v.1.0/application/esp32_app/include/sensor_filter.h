/**
 * @file sensor_filter.h
 * @brief Moving average filter for flex sensor data (ESP32 brain)
 * 
 * Receives raw 12-bit ADC values from STM32 via UART, applies
 * moving average filter, provides filtered values for gesture classification.
 * 
 * NEW ARCHITECTURE (v2.0):
 * - STM32: ADC acquisition only → sends MSG_TYPE_RAW_ADC
 * - ESP32: Filtering, classification, calibration → sends MSG_TYPE_GESTURE
 */

#ifndef SENSOR_FILTER_H
#define SENSOR_FILTER_H

#include <stdint.h>
#include <stdbool.h>
#include "common_types.h"

/**
 * @brief Initialize filter subsystem
 * 
 * Allocates buffers, resets state.
 * 
 * @return 0 on success, negative on error
 */
int sensor_filter_init(void);

/**
 * @brief Update filter with new raw ADC sample
 * 
 * @param channel Channel index (0-3)
 * @param raw_value Raw 12-bit ADC value (0-4095)
 * @return Filtered value (moving average)
 */
uint16_t sensor_filter_update(uint8_t channel, uint16_t raw_value);

/**
 * @brief Get current filtered value for a channel
 * 
 * @param channel Channel index (0-3)
 * @return Filtered value, or 0 if channel invalid
 */
uint16_t sensor_filter_get(uint8_t channel);

/**
 * @brief Get all filtered values at once
 * 
 * @param out Output array of NUM_FLEX_SENSORS elements
 */
void sensor_filter_get_all(uint16_t out[NUM_FLEX_SENSORS]);

/**
 * @brief Reset filter state (e.g., on calibration start)
 */
void sensor_filter_reset(void);

/**
 * @brief Get filter window size
 * 
 * @return Number of samples in moving average window
 */
uint8_t sensor_filter_get_window_size(void);

#endif /* SENSOR_FILTER_H */
