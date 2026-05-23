/**
 * @file sensor_logic.c
 * @brief STM32 Sensor Hub LITE (v2.0 Architecture)
 *
 * NEW ROLE:
 * - STM32: ADC acquisition ONLY → sends RAW_ADC frames to ESP32 @ 50Hz
 * - ESP32: Filtering + Classification + Calibration → sends GESTURE back
 * - STM32: Receives gesture → triggers haptic feedback
 *
 * REMOVED:
 * - filter_update() → moved to ESP32
 * - classify_gesture() → moved to ESP32
 *
 * MEMORY SAVED:
 * - filter_buf[4][8] = 64 bytes RAM
 * - filter_sum[4] = 16 bytes RAM
 * - filter_idx[4] = 4 bytes RAM
 * - classify_gesture() = ~2 KB flash
 *
 * Channels: PA0 (index), PA1 (middle), PA2 (ring), PA3 (pinky)
 * Voltage divider with 1kΩ fixed resistors, 12-bit ADC, 3.3 V reference.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "common_types.h"
#include "error_codes.h"
#include "safety_diag.h"
#include "sensor_logic.h"
#include "calibration.h"
#include "haptic_ui.h"
#include "frame_protocol.h"

LOG_MODULE_REGISTER(sensor_lite, CONFIG_LOG_DEFAULT_LEVEL);

/* Message queue defined in main.c */
extern struct k_msgq sensor_msgq;

/* ---------- ADC channel configuration via DeviceTree ---------- */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
    !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "DeviceTree node 'zephyr,user' with 'io-channels' required"
#endif

static const struct adc_dt_spec adc_channels[NUM_FLEX_SENSORS] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 3),
};

/* ---------- Gesture state (received from ESP32) ---------- */
static uint8_t g_current_gesture = GESTURE_NONE;

void sensor_set_gesture(uint8_t gesture_id)
{
    if (gesture_id != g_current_gesture) {
        g_current_gesture = gesture_id;
        LOG_INF("Gesture updated from ESP32: %u", gesture_id);
    }
}

uint8_t sensor_get_current_gesture(void)
{
    return g_current_gesture;
}

/* Praguri per-deget — populate din NVS prin calibration API la init */
/* NOTE: Phase 2 will move calibration to ESP32 */
static uint16_t k_thresh[NUM_FLEX_SENSORS];

/* ---------- Thread entry point ---------- */
void sensor_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int calib_rc = calibration_init();
    if (calib_rc < 0) {
        LOG_WRN("calibration_init failed: %d — folosesc defaults", calib_rc);
    }
    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        k_thresh[i] = calibration_get_thresh(i);
    }
    LOG_INF("Praguri active: I=%u M=%u R=%u P=%u",
            k_thresh[FINGER_INDEX],  k_thresh[FINGER_MIDDLE],
            k_thresh[FINGER_RING],   k_thresh[FINGER_PINKY]);

    int ret;
    int16_t adc_buf;
    struct adc_sequence seq = {
        .buffer      = &adc_buf,
        .buffer_size = sizeof(adc_buf),
    };

    for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
        if (!adc_is_ready_dt(&adc_channels[ch])) {
            LOG_ERR("ADC ch%u not ready", ch);
            return;
        }
        ret = adc_channel_setup_dt(&adc_channels[ch]);
        if (ret < 0) {
            LOG_ERR("ADC ch%u setup failed: %d", ch, ret);
            return;
        }
    }

    LOG_INF("Sensor thread started — %u ms period", SENSOR_SAMPLE_PERIOD_MS);
    LOG_INF("STM32-LITE (v2.0): Sending RAW_ADC frames @ 50Hz to ESP32");

    /* Fist-hold 20s → trigger recalibration */
#define FIST_CALIB_HOLD_MS  20000U
    int64_t fist_hold_since_ms = 0;
    bool    fist_hold_active   = false;
    bool    fist_calib_fired   = false;

    while (1) {
        sensor_packet_t pkt = {0};
        uint8_t faults = 0U;

        /* Debug heartbeat every 50 cycles (~5s @ 100ms) */
        static uint32_t loop_counter = 0;
        if ((loop_counter++ % 50) == 0) {
            LOG_INF("Sensor loop alive: cycle #%u", loop_counter);
        }

        if (calibration_take_start_request()) {
            LOG_INF("Cerere calibrare primita - pornire sesiune automata");
            haptic_notify_message();
            int rc = calibration_start(adc_channels);
            if (rc == 0) {
                for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
                    k_thresh[i] = calibration_get_thresh(i);
                }
                LOG_INF("Praguri actualizate dupa calibrare: I=%u M=%u R=%u P=%u",
                        k_thresh[FINGER_INDEX], k_thresh[FINGER_MIDDLE],
                        k_thresh[FINGER_RING], k_thresh[FINGER_PINKY]);
                haptic_notify_gesture(GESTURE_NONE);
            } else {
                LOG_ERR("Calibrare esuata: %d", rc);
                haptic_notify_error();
            }
        }

        for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
            k_thresh[ch] = calibration_get_thresh(ch);
            (void)adc_sequence_init_dt(&adc_channels[ch], &seq);
            ret = adc_read_dt(&adc_channels[ch], &seq);

            if (ret < 0) {
                LOG_WRN("ADC ch%u read error: %d", ch, ret);
                pkt.flex_raw[ch] = 0U;
                faults++;
                continue;
            }

            pkt.flex_raw[ch] = (uint16_t)adc_buf;

            /* Basic range validation */
            if (pkt.flex_raw[ch] > (uint16_t)ADC_MAX_VALID) {
                LOG_WRN("ADC ch%u out of range: %u", ch, pkt.flex_raw[ch]);
                faults++;
            }
        }

        /* Put packet in msgq → UART thread will send RAW_ADC frame */
        extern struct k_msgq sensor_msgq;
        pkt.gesture_id = g_current_gesture; /* Gesture set by ESP32 via MSG_TYPE_GESTURE */
        pkt.confidence = 0U;
        pkt.status_flags = 0U; /* No fault reporting for now */

        if (faults < NUM_FLEX_SENSORS) {
            int rc = k_msgq_put(&sensor_msgq, &pkt, K_NO_WAIT);
            if (rc < 0) {
                LOG_WRN("sensor_msgq full, dropping packet");
            }

            /* Debug: log raw ADC periodically */
            static uint32_t log_counter = 0;
            if ((log_counter++ % 50) == 0) { /* Every 1s @ 50Hz */
                LOG_DBG("RAW ADC: %u, %u, %u, %u → queued for ESP32",
                        pkt.flex_raw[0], pkt.flex_raw[1], pkt.flex_raw[2], pkt.flex_raw[3]);
            }
        } else {
            LOG_ERR("Too many sensor faults (%u) — skipping cycle", faults);
        }

        /* Fist-hold 20s calibration (based on gesture received from ESP32) */
        if (g_current_gesture == GESTURE_FIST) {
            int64_t now_ms = k_uptime_get();
            if (!fist_hold_active) {
                fist_hold_since_ms = now_ms;
                fist_hold_active   = true;
                fist_calib_fired   = false;
            } else if (!fist_calib_fired &&
                       ((now_ms - fist_hold_since_ms) >= (int64_t)FIST_CALIB_HOLD_MS)) {
                LOG_INF("FIST held 20s — triggering auto-calibration");
                calibration_request_start();
                fist_calib_fired = true;
            }
        } else {
            fist_hold_active = false;
            fist_calib_fired = false;
        }

        k_msleep(SENSOR_SAMPLE_PERIOD_MS);
    }
}
