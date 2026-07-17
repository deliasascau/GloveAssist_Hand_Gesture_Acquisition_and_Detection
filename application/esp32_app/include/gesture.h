#ifndef GESTURE_H
#define GESTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "frame_protocol.h"

/*
 * Flex sensor threshold fallback: ADC below this = finger bent.
 * Hardware: 3.3V -> 1k resistor -> ADC -> flex sensor -> GND.
 *   Extended: high resistance -> high ADC.
 *   Bent:     low resistance  -> low ADC.
 * Default 2048 is used only before a valid calibration profile is loaded.
 */
#define FLEX_THRESH       2048U

/*
 * Gesture must be stable for this many ms before being reported.
 * 1000 ms keeps reporting deliberate while still feeling responsive.
 */
#define GESTURE_STABLE_MS  1000U

/**
 * @brief Load per-finger calibration profiles.
 *
 * open_avg[i]: ADC average when finger i is fully extended (NONE phase).
 * fist_avg[i]: ADC average when finger i is bent in its target gesture:
 *   [0] = index  finger bent ADC (WATER phase)
 *   [1] = middle finger bent ADC (WC phase)
 *   [2] = ring   finger bent ADC (FOOD phase)
 *   [3] = pinky  finger bent ADC (HELP phase)
 *
 * After this call, gesture_classify() uses normalized hysteresis scoring.
 * Before this call it falls back to binary FLEX_THRESH comparison.
 */
bool gesture_set_profiles(const uint16_t open_avg[4], const uint16_t fist_avg[4]);

/** @return True if calibration profiles have been loaded. */
bool gesture_has_profiles(void);

/**
 * @brief Classify a gesture from 4 ADC flex sensor values.
 *
 * With profiles: each finger gets a 0-100 bent score with hysteresis bands
 * (score > 65 = bent sticky, score < 35 = extended sticky, in between = keep state).
 * Current accepted masks:
 * WATER(0001 or 1110), WC(0011), FOOD(0111), HELP(1111).
 *
 * Without profiles: binary FLEX_THRESH fallback.
 *
 * @param adc  [index, middle, ring, pinky] 12-bit ADC values.
 * @return Detected gesture_id_t.
 */
gesture_id_t gesture_classify(const uint16_t adc[4]);

/** @return Last bent-finger mask computed by gesture_classify(). */
uint8_t gesture_last_bent_mask(void);

/**
 * @brief Return a human-readable name for a gesture.
 */
const char *gesture_name(gesture_id_t g);

#endif /* GESTURE_H */
