/**
 * @file sensor_logic_lite.c
 * @brief STM32 Sensor Hub - LITE version (v2.0 Architecture)
 * 
 * NEW ARCHITECTURE:
 * - STM32: ADC acquisition ONLY → sends RAW_ADC frames to ESP32
 * - ESP32: Filtering, classification, calibration → sends GESTURE back
 * - STM32: Receives gesture → triggers haptic feedback
 * 
 * REMOVED from STM32:
 * - filter_update() → moved to ESP32
 * - classify_gesture() → moved to ESP32
 * - calibration logic → will move to ESP32 in Phase 2
 * 
 * MEMORY SAVED:
 * - filter_buf[4][8] = 64 bytes RAM
 * - filter_sum[4] = 16 bytes RAM
 * - filter_idx[4] = 4 bytes RAM
 * - classify_gesture() = ~2 KB flash
 * TOTAL: ~84 bytes RAM + ~2 KB flash freed!
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
#include "uart_comm.h"
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

/* Global gesture state (received from ESP32) */
static uint8_t g_current_gesture = GESTURE_NONE;
static bool g_gesture_active = false;

void sensor_set_gesture(uint8_t gesture_id)
{
    if (gesture_id != g_current_gesture) {
        g_current_gesture = gesture_id;
        g_gesture_active = (gesture_id != GESTURE_NONE);
        
        /* Trigger haptic feedback */
        if (g_gesture_active) {
            haptic_notify_gesture(gesture_id);
            LOG_INF("Gesture received from ESP32: %u", gesture_id);
        }
    }
}

uint8_t sensor_get_current_gesture(void)
{
    return g_current_gesture;
}

/* ---------- Thread entry point ---------- */
void sensor_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("=== STM32 Sensor Hub LITE (v2.0) ===");
    LOG_INF("Role: ADC acquisition → RAW_ADC frames to ESP32");
    LOG_INF("ESP32 does: Filtering, Classification, Calibration");

    /* Initialize calibration (for now, will move to ESP32 in Phase 2) */
    int calib_rc = calibration_init();
    if (calib_rc < 0) {
        LOG_WRN("calibration_init failed: %d — using defaults", calib_rc);
    }

    int ret;
    int16_t adc_buf;
    struct adc_sequence seq = {
        .buffer      = &adc_buf,
        .buffer_size = sizeof(adc_buf),
    };

    /* Setup ADC channels */
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
    LOG_INF("Sending RAW_ADC frames @ 50Hz to ESP32 for processing");

    /* Fist-hold 20s → trigger recalibration (TODO: move to ESP32) */
#define FIST_CALIB_HOLD_MS  20000U
    int64_t fist_hold_since_ms = 0;
    bool    fist_hold_active   = false;
    bool    fist_calib_fired   = false;

    while (1) {
        uint16_t raw_adc[NUM_FLEX_SENSORS];
        uint8_t faults = 0U;

        /* Handle calibration request (will move to ESP32 in Phase 2) */
        if (calibration_take_start_request()) {
            LOG_INF("Calibration request received");
            haptic_notify_message();
            int rc = calibration_start(adc_channels);
            if (rc == 0) {
                LOG_INF("Calibration completed");
                haptic_notify_gesture(GESTURE_NONE);
            } else {
                LOG_ERR("Calibration failed: %d", rc);
                haptic_notify_error();
            }
        }

        /* Read raw ADC from all 4 channels */
        for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
            (void)adc_sequence_init_dt(&adc_channels[ch], &seq);
            ret = adc_read_dt(&adc_channels[ch], &seq);

            if (ret < 0) {
                LOG_WRN("ADC ch%u read error: %d", ch, ret);
                raw_adc[ch] = 0U;
                faults++;
                continue;
            }

            raw_adc[ch] = (uint16_t)adc_buf;

            /* Basic range validation */
            if (raw_adc[ch] > (uint16_t)ADC_MAX_VALID) {
                LOG_WRN("ADC ch%u out of range: %u", ch, raw_adc[ch]);
                faults++;
            }
        }

        /* Send RAW_ADC frame to ESP32 (if no major faults) */
        if (faults < NUM_FLEX_SENSORS) {
            frame_raw_adc_payload_t raw_payload;
            for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
                raw_payload.raw[i] = raw_adc[i];
            }

            glove_frame_t tx_frame;
            int32_t rc = frame_build(&tx_frame, MSG_TYPE_RAW_ADC,
                                     (const uint8_t *)&raw_payload,
                                     sizeof(raw_payload));
            if (rc >= 0) {
                /* Send via UART (uart_comm_send defined in uart_comm.c) */
                extern int uart_comm_send_raw(const glove_frame_t *frame);
                uart_comm_send_raw(&tx_frame);
            }

            /* Debug: log raw ADC periodically */
            static uint32_t log_counter = 0;
            if ((log_counter++ % 50) == 0) { /* Every 1s @ 50Hz */
                LOG_DBG("RAW ADC: %u, %u, %u, %u → sent to ESP32",
                        raw_adc[0], raw_adc[1], raw_adc[2], raw_adc[3]);
            }
        }

        /* Fist-hold calibration trigger (TODO: move to ESP32 Phase 2) */
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

        /* Sleep until next sample period */
        k_msleep(SENSOR_SAMPLE_PERIOD_MS);
    }
}
