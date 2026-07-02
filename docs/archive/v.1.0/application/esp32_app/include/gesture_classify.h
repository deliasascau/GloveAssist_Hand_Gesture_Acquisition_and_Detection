/**
 * @file gesture_classify.h
 * @brief Gesture classification engine (ESP32 brain)
 * 
 * Takes filtered sensor values, applies threshold-based rules
 * to detect gestures.
 * 
 * NEW ARCHITECTURE (v2.0):
 * - STM32: Dumb sensor hub
 * - ESP32: Smart classification → sends MSG_TYPE_GESTURE to STM32
 */

#ifndef GESTURE_CLASSIFY_H
#define GESTURE_CLASSIFY_H

#include <stdint.h>
#include <stdbool.h>
#include "common_types.h"

/**
 * @brief Initialize gesture classifier
 * 
 * Loads thresholds from NVS (ESP32-local storage).
 * 
 * @return 0 on success, negative on error
 */
int gesture_classify_init(void);

/**
 * @brief Classify gesture from filtered sensor values
 * 
 * @param filtered Array of NUM_FLEX_SENSORS filtered ADC values
 * @return Gesture ID (GESTURE_NONE, GESTURE_FIST, etc.)
 */
uint8_t gesture_classify(const uint16_t filtered[NUM_FLEX_SENSORS]);

/**
 * @brief Update thresholds (e.g., after calibration)
 * 
 * @param thresh Array of NUM_FLEX_SENSORS threshold values
 * @return 0 on success, negative on error
 */
int gesture_classify_update_thresholds(const uint16_t thresh[NUM_FLEX_SENSORS]);

/**
 * @brief Get current threshold for a finger
 * 
 * @param finger Finger index (FINGER_INDEX, etc.)
 * @return Threshold value, or 0 if invalid
 */
uint16_t gesture_classify_get_threshold(uint8_t finger);

/**
 * @brief Get all thresholds at once
 * 
 * @param out Output array of NUM_FLEX_SENSORS elements
 */
void gesture_classify_get_all_thresholds(uint16_t out[NUM_FLEX_SENSORS]);

#endif /* GESTURE_CLASSIFY_H */
