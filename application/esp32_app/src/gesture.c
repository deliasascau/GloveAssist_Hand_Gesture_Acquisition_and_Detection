/*
 * GloveAssist - Gesture Classifier with per-finger hysteresis scoring.
 *
 * Hardware: flex sensor + voltage divider → HIGH ADC = finger EXTENDED,
 *           LOW ADC = finger BENT (less resistance when bent).
 *
 * After per-gesture calibration, each finger i gets:
 *   open_avg[i]: ADC when finger extended (from NONE calibration phase)
 *   fist_avg[i]: ADC when finger bent     (extracted from per-gesture phases:
 *                index from WATER, middle from WC, ring from FOOD, pinky from HELP)
 *
 * Normalized bent score per finger (0-100):
 *   0   = fully extended (ADC near open_avg)
 *   100 = fully bent     (ADC near fist_avg)
 *
 * Hysteresis bands prevent oscillation near the 50% point:
 *   score > BENT_THRESH  → finger classified as BENT   (state updated)
 *   score < EXT_THRESH   → finger classified as EXTENDED (state updated)
 *   score in [EXT..BENT] → ambiguous, keep previous state (sticky)
 *
 * Gesture mapping (bent-finger bit mask):
 *   bit 0 = index, bit 1 = middle, bit 2 = ring, bit 3 = pinky
 *   0b0001 → WATER  (index only)
 *   0b0011 → WC     (index + middle)
 *   0b0111 → FOOD   (index + middle + ring)
 *   0b1111 → HELP   (all four = fist)
 *   0b0000 → NONE   (all extended)
 */

#include "gesture.h"
#include <string.h>
#include <stdbool.h>

#define BENT_THRESH   65U   /* score > 65 → clearly bent      */
#define EXT_THRESH    35U   /* score < 35 → clearly extended   */
#define MIN_RANGE    250U   /* minimum open-fist ADC gap for profiles to be valid */

/* Calibration profiles */
static uint16_t s_open[4];
static uint16_t s_fist[4];
static bool     s_has_profiles;

/* Per-finger sticky bend state: 0 = extended, 1 = bent */
static uint8_t  s_bent[4];
static uint8_t  s_last_bent_mask;

/* ── Internal ────────────────────────────────────────────────────────────── */

/*
 * Returns 0-100 bent score for finger i.
 * Falls back to binary 0/100 if profiles are absent or range is too small.
 */
static uint8_t finger_score(uint8_t i, uint16_t adc)
{
    if (!s_has_profiles) {
        static const uint16_t s_thresh[4] = {
            FLEX_THRESH, FLEX_THRESH, FLEX_THRESH, FLEX_THRESH
        };
        return (adc < s_thresh[i]) ? 100U : 0U;
    }

    int32_t sc;
    if ((uint32_t)s_open[i] >= ((uint32_t)s_fist[i] + MIN_RANGE)) {
        /* Common divider: open ADC high, bent/fist ADC low. */
        int32_t range = (int32_t)s_open[i] - (int32_t)s_fist[i];
        int32_t pos   = (int32_t)s_open[i] - (int32_t)adc;
        sc = (pos * 100) / range;
    } else if ((uint32_t)s_fist[i] >= ((uint32_t)s_open[i] + MIN_RANGE)) {
        /* Inverted divider or sensor mounting: open ADC low, bent/fist ADC high. */
        int32_t range = (int32_t)s_fist[i] - (int32_t)s_open[i];
        int32_t pos   = (int32_t)adc       - (int32_t)s_open[i];
        sc = (pos * 100) / range;
    } else {
        static const uint16_t s_thresh[4] = {
            FLEX_THRESH, FLEX_THRESH, FLEX_THRESH, FLEX_THRESH
        };
        return (adc < s_thresh[i]) ? 100U : 0U;
    }

    if (sc < 0)   { sc = 0;   }
    if (sc > 100) { sc = 100; }
    return (uint8_t)sc;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

bool gesture_set_profiles(const uint16_t open_avg[4], const uint16_t fist_avg[4])
{
    bool valid = true;

    for (uint8_t i = 0U; i < 4U; i++) {
        s_open[i] = open_avg[i];
        s_fist[i] = fist_avg[i];
        uint16_t gap = (s_open[i] > s_fist[i])
            ? (uint16_t)(s_open[i] - s_fist[i])
            : (uint16_t)(s_fist[i] - s_open[i]);
        if (gap < MIN_RANGE) {
            valid = false;
        }
    }
    s_has_profiles = valid;
    (void)memset(s_bent, 0, sizeof(s_bent));  /* reset sticky state */
    s_last_bent_mask = 0U;
    return valid;
}

bool gesture_has_profiles(void)
{
    return s_has_profiles;
}

gesture_id_t gesture_classify(const uint16_t adc[4])
{
    uint8_t bent_mask = 0U;

    for (uint8_t i = 0U; i < 4U; i++) {
        uint8_t sc = finger_score(i, adc[i]);

        if (sc > BENT_THRESH) {
            s_bent[i] = 1U;   /* clearly bent */
        } else if (sc < EXT_THRESH) {
            s_bent[i] = 0U;   /* clearly extended */
        }
        /* else: ambiguous — keep previous state (hysteresis) */

        bent_mask |= (uint8_t)((uint8_t)(s_bent[i] != 0U) << i);
    }
    s_last_bent_mask = bent_mask;

    switch (bent_mask) {
    case 0x0FU: return GESTURE_HELP;   /* all 4 bent / fist */
    case 0x07U: /* index+middle+ring bent */
        return GESTURE_FOOD;
    case 0x03U: /* index+middle bent */
        return GESTURE_WC;
    case 0x01U: /* index only bent */
    case 0x0EU: /* index raised/extended, other three bent (pointing) */
        return GESTURE_WATER;
    case 0x00U: return GESTURE_NONE;   /* all extended */
    default:    return GESTURE_NONE;   /* unrecognized combination */
    }
}

uint8_t gesture_last_bent_mask(void)
{
    return s_last_bent_mask;
}

const char *gesture_name(gesture_id_t g)
{
    switch (g) {
    case GESTURE_WATER:   return "WATER";
    case GESTURE_WC:      return "WC";
    case GESTURE_FOOD:    return "FOOD";
    case GESTURE_HELP:    return "HELP";
    case GESTURE_CONFIRM: return "OK";
    default:              return "NONE";
    }
}
