/**
 * @file gesture_classify.c
 * @brief Gesture classification engine (ESP32 brain)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "gesture_classify.h"

LOG_MODULE_REGISTER(gesture_classify, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Thresholds (loaded from NVS or defaults) ---------- */
static uint16_t k_thresh[NUM_FLEX_SENSORS];
static bool initialized = false;

/* Default thresholds (from original STM32 calibration data) */
static const uint16_t k_defaults[NUM_FLEX_SENSORS] = {
    621U,   /* FINGER_INDEX  */
    874U,   /* FINGER_MIDDLE */
    2009U,  /* FINGER_RING   */
    944U,   /* FINGER_PINKY  */
};

int gesture_classify_init(void)
{
    if (initialized) {
        LOG_WRN("gesture_classify already initialized");
        return 0;
    }

    /* TODO: Load thresholds from ESP32 NVS */
    /* For now, use defaults */
    (void)memcpy(k_thresh, k_defaults, sizeof(k_thresh));

    initialized = true;
    LOG_INF("Gesture classifier initialized");
    LOG_INF("Thresholds: I=%u M=%u R=%u P=%u",
            k_thresh[FINGER_INDEX], k_thresh[FINGER_MIDDLE],
            k_thresh[FINGER_RING], k_thresh[FINGER_PINKY]);
    return 0;
}

uint8_t gesture_classify(const uint16_t filtered[NUM_FLEX_SENSORS])
{
    if (!initialized || filtered == NULL) {
        return GESTURE_NONE;
    }

    uint8_t bent_count = 0U;
    uint8_t single_idx = 0U;
    bool bent[NUM_FLEX_SENSORS];

    /* 
     * Circuit: 3.3V → [1kΩ] → PA_x → [flex sensor] → GND
     * Bent sensor: lower resistance → higher voltage → higher ADC
     * BUT: we have pull-down resistors → bent = LOWER ADC value
     * 
     * Rule: ADC < threshold → finger bent
     */
    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        bent[i] = (filtered[i] < k_thresh[i]);
        if (bent[i]) {
            single_idx = i;
            bent_count++;
        }
    }

    /* All fingers bent → FIST */
    if (bent_count == (uint8_t)NUM_FLEX_SENSORS) {
        return GESTURE_FIST;
    }

    /* Only one finger bent → specific gesture */
    if (bent_count == 1U) {
        return (uint8_t)(GESTURE_INDEX + single_idx);
    }

    /* Ring + Pinky bent, Index + Middle open → HELP */
    if ((bent_count == 2U) &&
        bent[FINGER_RING] && bent[FINGER_PINKY] &&
        !bent[FINGER_INDEX] && !bent[FINGER_MIDDLE]) {
        return GESTURE_HELP;
    }

    /* No gesture detected */
    return GESTURE_NONE;
}

int gesture_classify_update_thresholds(const uint16_t thresh[NUM_FLEX_SENSORS])
{
    if (!initialized || thresh == NULL) {
        return -EINVAL;
    }

    /* Validate thresholds */
    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        if (thresh[i] < (uint16_t)ADC_MIN_VALID ||
            thresh[i] > (uint16_t)ADC_MAX_VALID) {
            LOG_ERR("Invalid threshold for finger %u: %u", i, thresh[i]);
            return -EINVAL;
        }
    }

    (void)memcpy(k_thresh, thresh, sizeof(k_thresh));

    LOG_INF("Thresholds updated: I=%u M=%u R=%u P=%u",
            k_thresh[FINGER_INDEX], k_thresh[FINGER_MIDDLE],
            k_thresh[FINGER_RING], k_thresh[FINGER_PINKY]);

    /* TODO: Save to ESP32 NVS */

    return 0;
}

uint16_t gesture_classify_get_threshold(uint8_t finger)
{
    if (!initialized || finger >= (uint8_t)NUM_FLEX_SENSORS) {
        return 0U;
    }

    return k_thresh[finger];
}

void gesture_classify_get_all_thresholds(uint16_t out[NUM_FLEX_SENSORS])
{
    if (!initialized || out == NULL) {
        return;
    }

    (void)memcpy(out, k_thresh, sizeof(k_thresh));
}
