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
#define MIN_RANGE    100U   /* minimum open-fist ADC gap for profiles to be valid */

/* Calibration profiles */
static uint16_t s_open[4];
static uint16_t s_fist[4];
static bool     s_has_profiles;

/* Per-finger sticky bend state: 0 = extended, 1 = bent */
static uint8_t  s_bent[4];

/* ── Internal ────────────────────────────────────────────────────────────── */

/*
 * Returns 0-100 bent score for finger i.
 * Falls back to binary 0/100 if profiles are absent or range is too small.
 */
static uint8_t finger_score(uint8_t i, uint16_t adc)
{
    /* Profiles valid when open ADC > fist ADC + MIN_RANGE (open=high, fist=low). */
    if (!s_has_profiles ||
        ((uint32_t)s_open[i] < (uint32_t)s_fist[i] + MIN_RANGE)) {
        static const uint16_t s_thresh[4] = {
            FLEX_THRESH, FLEX_THRESH, FLEX_THRESH, FLEX_THRESH
        };
        return (adc < s_thresh[i]) ? 100U : 0U;
    }

    /* range is negative (fist_low - open_high < 0); pos is also negative toward fist. */
    int32_t range = (int32_t)s_fist[i] - (int32_t)s_open[i];
    int32_t pos   = (int32_t)adc       - (int32_t)s_open[i];
    int32_t sc    = (pos * 100) / range;

    if (sc < 0)   { sc = 0;   }
    if (sc > 100) { sc = 100; }
    return (uint8_t)sc;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void gesture_set_profiles(const uint16_t open_avg[4], const uint16_t fist_avg[4])
{
    for (uint8_t i = 0U; i < 4U; i++) {
        s_open[i] = open_avg[i];
        s_fist[i] = fist_avg[i];
    }
    s_has_profiles = true;
    (void)memset(s_bent, 0, sizeof(s_bent));  /* reset sticky state */
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

    switch (bent_mask) {
    case 0x0FU: return GESTURE_HELP;   /* all 4 bent */
    case 0x07U: return GESTURE_FOOD;   /* index+middle+ring bent */
    case 0x03U: return GESTURE_WC;     /* index+middle bent */
    case 0x01U: return GESTURE_WATER;  /* index only bent */
    case 0x00U: return GESTURE_NONE;   /* all extended */
    default:    return GESTURE_NONE;   /* unrecognized combination */
    }
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
