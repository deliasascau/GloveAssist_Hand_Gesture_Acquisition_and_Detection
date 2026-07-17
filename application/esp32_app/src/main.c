/*
 * GloveAssist ESP32 - Main Application
 *
 * Coordinates 3 modules:
 *   uart_stm32 : UART2 communication with STM32 (TX/RX frames)
 *   gesture    : Threshold-based flex sensor gesture classifier
 *   ble_adc    : BLE NUS peripheral (notify phone, receive caregiver ACK)
 *
 * Main loop:
 *   - Receives SENSOR_RAW frames from STM32 via UART2
 *   - Classifies gesture with compose + 1000ms stability filter
 *   - Notifies BLE phone with gesture name and publishes over MQTT
 *   - Sends HEARTBEAT to STM32 every 1000ms
 *   - On BLE caregiver "OK" -> uart_stm32_send_caregiver_ack()
 *     -> STM32 plays OLED + buzzer + motor feedback
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

#include "frame_protocol.h"
#include "gesture.h"
#include "uart_stm32.h"
#include "ble_adc.h"
#include "calibration.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(esp32_main, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD_MS  1000U
#define STATUS_REPORT_MS     3000U
#define STM32_FRAME_TIMEOUT_MS 3000U
#define RAW_LOG_COUNT          16U
#define GESTURE_RETRIGGER_MS 5000U  /* dupa 5s, acelasi gest poate re-triggera */
#define GESTURE_COMPOSE_COUNT   5U  /* N frame-uri consecutive inainte de debounce (100ms la 50Hz) */
#define RECAL_HOLD_MS       20000U  /* pumn inchis 20s continuu -> recalibrare */

/* -- Link statistics ---------------------------------------------------- */
static uint32_t s_total_frames;
static uint32_t s_bad_frames;
static uint32_t s_total_bytes;
static uint32_t s_seq_gaps;
static uint8_t  s_last_seq    = 0xFFU; /* 0xFF = no frame yet */

/* -- Gesture stability filter state ------------------------------------- */
static gesture_id_t s_last_stable_gesture;
static gesture_id_t s_pending_gesture;
static uint32_t     s_gesture_stable_since;
static uint32_t     s_last_stable_time;   /* moment ultimului gest raportat */
static bool         s_midpoint_buzzed;    /* buzz de progres la 1s */

/* Compose window: N consecutive frames must agree before debounce starts */
static gesture_id_t s_compose_gesture = GESTURE_NONE;
static uint8_t      s_compose_count   = 0U;

/* -- Fist-hold recalibration tracker ------------------------------------ */
static uint32_t s_fist_hold_start = 0U;  /* 0 = fist not currently tracked */
static bool     s_recal_requested = false;

/* -- First-boot calibration flag ---------------------------------------- */
static bool s_need_calibration = false;  /* set if no NVS profiles found */

/* -- Last ADC snapshot (used for calibration; BLE runtime sends gestures only) */
static uint16_t s_last_adc[4];

/* -- UART session handshake state --------------------------------------- */
static uint32_t s_session_nonce;       /* nonce sent to STM32 in SESSION_HELLO */
static uint32_t s_session_retry_ts;   /* timestamp of last SESSION_HELLO send  */
static uint32_t s_last_stm32_rx_ts;   /* last valid frame received from STM32   */
static bool     s_session_established;
static frame_hmac_parser_t s_uart_parser;
#define SESSION_RETRY_MS  2000U        /* resend SESSION_HELLO every 2s until ACK */

/* -- Internal helpers --------------------------------------------------- */

static void reset_gesture_pipeline(void)
{
    s_last_stable_gesture = GESTURE_NONE;
    s_pending_gesture = GESTURE_NONE;
    s_gesture_stable_since = 0U;
    s_last_stable_time = 0U;
    s_midpoint_buzzed = false;
    s_compose_gesture = GESTURE_NONE;
    s_compose_count = 0U;
    s_fist_hold_start = 0U;
    s_recal_requested = false;
}

static void check_seq(uint8_t seq)
{
    if (s_last_seq == 0xFFU) {
        s_last_seq = seq;
        return;
    }
    uint8_t expected = (uint8_t)(s_last_seq + 1U);
    if (seq != expected) {
        s_seq_gaps++;
        LOG_WRN("SEQ gap: expected %u got %u", expected, seq);
    }
    s_last_seq = seq;
}

static void start_uart_session(uint32_t now, const char *reason)
{
    uint32_t nonce;

    nonce = esp_random();  /* hardware RNG — zgomot termic real, nu pseudo-random */
    if (nonce == 0U) {
        nonce = 0xA5C37B29U;
    }

    s_session_nonce = nonce;
    s_session_retry_ts = now;
    s_last_stm32_rx_ts = now;
    s_session_established = false;
    s_last_seq = 0xFFU;

    frame_secure_set_session(nonce);
    frame_hmac_parser_init(&s_uart_parser);
    reset_gesture_pipeline();
    uart_stm32_send_session_hello(nonce);

    if (reason != NULL) {
        LOG_WRN("%s (nonce=0x%08x)", reason, nonce);
    } else {
        LOG_INF("UART secure session start (nonce=0x%08x)", nonce);
    }
}

static void handle_sensor_frame(const frame_t *f)
{
    uint16_t adc[4];

    frame_decode_sensor_payload(f->payload, &adc[0], &adc[1], &adc[2], &adc[3]);
    s_session_established = true;
    check_seq(f->seq);
    s_total_frames++;
    uart_stm32_send_ack(f->seq);

    /* Keep latest ADC values for calibration display and profile sampling. */
    s_last_adc[0] = adc[0];
    s_last_adc[1] = adc[1];
    s_last_adc[2] = adc[2];
    s_last_adc[3] = adc[3];

    /*
     * During calibration: skip gesture classification entirely.
     * The calibration module reads ADC values via calibration_update() in
     * the main loop; we must not interfere with gesture state here.
     */
    if (calibration_active()) {
        return;
    }

    gesture_id_t g   = gesture_classify(adc);
    uint32_t     now = k_uptime_get_32();

    /*
     * Fist-hold recalibration tracker (checked before compose window).
     * If GESTURE_HELP (full fist) is classified continuously for RECAL_HOLD_MS,
     * set s_recal_requested flag for the main loop to handle.
     */
    if (g == GESTURE_HELP) {
        if (s_fist_hold_start == 0U) {
            s_fist_hold_start = now;
        } else if (!calibration_active()
                   && ((now - s_fist_hold_start) >= RECAL_HOLD_MS)) {
            s_fist_hold_start = 0U;
            s_recal_requested = true;
            LOG_INF("Fist held %u ms -> recalibration triggered", RECAL_HOLD_MS);
        }
    } else {
        s_fist_hold_start = 0U;  /* reset on any non-fist gesture */
    }

    /*
     * Compose window: GESTURE_COMPOSE_COUNT consecutive frames must
     * return the same gesture before we start the stability timer.
     * This eliminates single-frame spikes and transition noise.
     */
    if (g != s_compose_gesture) {
        s_compose_gesture = g;
        s_compose_count   = 1U;
        return; /* reset compose - don't advance stability timer */
    }
    if (s_compose_count < GESTURE_COMPOSE_COUNT) {
        s_compose_count++;
        return; /* still composing */
    }

    /* Composed gesture confirmed - run stability/debounce tracker */
    if (g != s_pending_gesture) {
        s_pending_gesture      = g;
        s_gesture_stable_since = now;
        s_midpoint_buzzed      = false;
    } else if (g != GESTURE_NONE) {
        uint32_t stable_ms = now - s_gesture_stable_since;
        /* Buzz la 1s: feedback de progres ("inca un pic") */
        if (!s_midpoint_buzzed && (stable_ms >= (GESTURE_STABLE_MS / 2U))) {
            s_midpoint_buzzed = true;
            uart_stm32_send_haptic_buzz(30U);
        }
    }
    if ((g != GESTURE_NONE)
               && (g == s_pending_gesture)
               && ((g != s_last_stable_gesture)
                   || ((now - s_last_stable_time) >= GESTURE_RETRIGGER_MS))
               && ((now - s_gesture_stable_since) >= GESTURE_STABLE_MS)) {
        s_last_stable_gesture = g;
        s_last_stable_time    = now;
        LOG_INF("GESTURE: %s (id=%u)  adc %u %u %u %u",
                gesture_name(g), (unsigned)g,
                adc[0], adc[1], adc[2], adc[3]);
        /* Local feedback has priority: patient confirmation should not wait
         * behind BLE notifications or cloud publishing. */
        uart_stm32_send_gesture_feedback((uint8_t)g);
        ble_adc_set_last_gesture(g);
        (void)ble_adc_send_gesture(g, adc);
        (void)wifi_mqtt_publish_gesture(g);
        (void)wifi_mqtt_publish_status(gesture_name(g));
    }
}

static void handle_frame(const frame_t *f)
{
    s_last_stm32_rx_ts = k_uptime_get_32();

    switch ((frame_type_t)f->type) {
    case FRAME_TYPE_SENSOR_RAW:
        handle_sensor_frame(f);
        break;
    case FRAME_TYPE_HEARTBEAT:
        uart_stm32_send_ack(f->seq);
        break;
    case FRAME_TYPE_STATUS:
        if (f->payload[0] == (uint8_t)STATUS_ERR) {
            if ((f->payload[1] | f->payload[2] | f->payload[3]) != 0U) {
                LOG_WRN("STM32 sensor fault: active=0x%02x low=0x%02x high=0x%02x",
                        f->payload[1], f->payload[2], f->payload[3]);
            } else {
                LOG_INF("STM32 sensor fault cleared");
            }
        } else {
            LOG_INF("STM32 status: 0x%02x", f->payload[0]);
        }
        break;
    case FRAME_TYPE_SESSION:
        /* SESSION_ACK from STM32: session handshake confirmed. */
        if (f->payload[0] == (uint8_t)SESSION_ACK) {
            s_session_established = true;
            s_last_seq = 0xFFU;
            LOG_INF("UART secure session established (STM32 ACK received)");
        }
        break;
    default:
        break;
    }
}

static void log_link_status(void)
{
    if (s_total_frames > 0U) {
        LOG_INF("Link: %u frames  %u bad  %u gaps  %u bytes",
                s_total_frames, s_bad_frames, s_seq_gaps, s_total_bytes);
    } else {
        LOG_WRN("No frames yet (bytes=%u bad=%u)", s_total_bytes, s_bad_frames);
    }
}

/* -- Main --------------------------------------------------------------- */
int main(void)
{
    LOG_INF("=== GloveAssist ESP32 ===");

    if (uart_stm32_init() != 0) {
        LOG_ERR("UART STM32 init failed");
        return -1;
    }

    start_uart_session(k_uptime_get_32(), NULL);

    bool ble_ready = false;
    if (ble_adc_init() != 0) {
        LOG_WRN("BLE init failed or disabled — continuing without BLE");
        /* Non-fatal: UART+WiFi+MQTT still work. BLE stubs return 0. */
    } else {
        ble_ready = true;
    }

    /*
     * Load saved per-gesture profiles from NVS.
     * Extract per-finger open/fist values for the hysteresis classifier:
     *   open[i]  = NONE phase   (all fingers extended)
     *   fist[0]  = WATER phase  (index  finger bent)
     *   fist[1]  = WC    phase  (middle finger bent)
     *   fist[2]  = FOOD  phase  (ring   finger bent)
     *   fist[3]  = HELP  phase  (pinky  finger bent)
     */
    {
        uint16_t saved_profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS];
        if (calibration_init(saved_profiles) != 0) {
            s_need_calibration = true;
            LOG_INF("First boot: no calibration data. Will auto-calibrate.");
        } else {
            uint16_t open_p[4], fist_p[4];
            for (uint8_t i = 0U; i < 4U; i++) {
                open_p[i] = saved_profiles[0][i]; /* NONE: all extended */
            }
            fist_p[0] = saved_profiles[1][0]; /* WATER: index  bent */
            fist_p[1] = saved_profiles[2][1]; /* WC:    middle bent */
            fist_p[2] = saved_profiles[3][2]; /* FOOD:  ring   bent */
            fist_p[3] = saved_profiles[4][3]; /* HELP:  pinky  bent */
            gesture_set_profiles(open_p, fist_p);
            LOG_INF("Gesture profiles loaded from NVS");
        }
    }

    /* Start WiFi+MQTT (no-op in BLE-only builds). */
    (void)wifi_mqtt_init();

    LOG_INF("Init OK. Waiting for STM32 frames on UART2...");
    if (ble_ready) {
        LOG_INF("BLE ready for phone connection");
    }

    frame_hmac_parser_init(&s_uart_parser);

    uint32_t last_hb_tx    = k_uptime_get_32();
    uint32_t last_report   = k_uptime_get_32();
    uint8_t  raw_log_count = 0U;

    while (1) {
        uint32_t now = k_uptime_get_32();
        uint8_t  b;

        ble_adc_process();

        if (s_session_established && ((now - last_hb_tx) >= HEARTBEAT_PERIOD_MS)) {
            last_hb_tx = now;
            uart_stm32_send_heartbeat();
        }

        if (s_session_established
            && ((now - s_last_stm32_rx_ts) >= STM32_FRAME_TIMEOUT_MS)) {
            start_uart_session(now, "STM32 link quiet - restarting UART session handshake");
        }

        /* Retry SESSION_HELLO until the current STM32 session is confirmed.
         * This also recovers when STM32 resets/flashes while ESP32 stays on. */
        if (!s_session_established && ((now - s_session_retry_ts) >= SESSION_RETRY_MS)) {
            s_session_retry_ts = now;
            uart_stm32_send_session_hello(s_session_nonce);
            LOG_DBG("SESSION_HELLO retry (nonce=0x%08x)", s_session_nonce);
        }

        if ((now - last_report) >= STATUS_REPORT_MS) {
            last_report = now;
            log_link_status();
        }

        /* Calibration request from BLE ('C'). */
        if (ble_adc_pop_calibration_request()) {
            if (calibration_active()) {
                (void)ble_adc_send_text("CALIB: active\n");
            } else if (s_total_frames == 0U) {
                (void)ble_adc_send_text("CALIB: no sensor\n");
                LOG_WRN("Calibration requested but no STM32 frames yet");
            } else {
                LOG_INF("BLE: starting calibration");
                uart_stm32_send_oled_text("CALIBR");
                uart_stm32_send_haptic_buzz(150U);
                calibration_start();
                (void)ble_adc_send_text("CALIB: starting\n");
            }
        }

        /*
         * Auto-calibration: first boot or missing NVS profiles.
         * Wait until at least one STM32 frame has arrived (sensors warm-up),
         * then start calibration automatically.
         */
        if (s_need_calibration && !calibration_active() && (s_total_frames > 0U)) {
            s_need_calibration = false;
            LOG_INF("Auto-starting calibration (first boot / no profiles)");
            uart_stm32_send_oled_text("CALIBR");
            uart_stm32_send_haptic_buzz(150U);
            uart_stm32_send_haptic_vibrate(200U);
            (void)ble_adc_send_text("CALIB: starting\n");
            calibration_start();
        }

        /*
         * Fist held 20s: trigger recalibration.
         * Flag is set by handle_sensor_frame when GESTURE_HELP held for RECAL_HOLD_MS.
         */
        if (s_recal_requested && !calibration_active()) {
            s_recal_requested = false;
            LOG_INF("Fist 20s held -> starting recalibration");
            uart_stm32_send_oled_text("RECAL");
            uart_stm32_send_haptic_buzz(200U);
            uart_stm32_send_haptic_vibrate(300U);
            (void)ble_adc_send_text("CALIB: starting\n");
            calibration_start();
        }

        /*
         * Caregiver ACK: caregiver sent "ok" / digit via BLE.
         * Forward to STM32 (OLED "Understood" + haptic), notify BLE side.
         * NOTE: ACK is processed regardless of whether a gesture was active —
         * if gesture is NONE the STM32 still plays the confirmation feedback.
         */
        {
            gesture_id_t ack_g = GESTURE_NONE;
            if (ble_adc_pop_caregiver_ack_request(&ack_g)) {
                uart_stm32_send_caregiver_ack((uint8_t)ack_g);
                (void)ble_adc_send_text("Understood\n");
                LOG_INF("Caregiver ACK: gesture %u -> STM32 + BLE", (unsigned)ack_g);
            }
        }

        /* Run calibration update if active. */
        if (calibration_active()) {
            /*
             * Phase-change detection + countdown 3-2-1.
             * OLED is driven by periodic CMD_OLED_CALIB (every 200ms) which shows
             * phase label + countdown + live ADC values on all 4 OLED rows.
             * Haptic (buzz + vibrate) fires on phase transitions and countdown ticks.
             */
            const char *phase_str = calibration_status_line();
            static const char *s_last_phase_str;
            static uint32_t    s_calib_phase_ts  = 0U;
            static uint32_t    s_calib_oled_ts   = 0U;
            static int8_t      s_calib_cnt        = -1;

            if (phase_str != s_last_phase_str) {
                s_last_phase_str = phase_str;
                s_calib_phase_ts = now;
                s_calib_cnt      = 3;
                /* Tell user which gesture to hold (P: = phase instruction). */
                char pmsg[20];
                (void)snprintf(pmsg, sizeof(pmsg), "P:%s\n", phase_str);
                (void)ble_adc_send_text(pmsg);
                uart_stm32_send_haptic_buzz(80U);
                uart_stm32_send_haptic_vibrate(100U);
                (void)ble_adc_send_text("CALIB:3\n");
            }

            /* Non-blocking countdown: 3 → 2 → 1 → TINE! — haptic only, OLED via periodic */
            if (s_calib_cnt >= 0) {
                uint32_t settle_el = now - s_calib_phase_ts;
                int8_t target_cnt;

                if (settle_el < (CALIB_SETTLE_MS / 3U)) {
                    target_cnt = 3;
                } else if (settle_el < ((CALIB_SETTLE_MS * 2U) / 3U)) {
                    target_cnt = 2;
                } else if (settle_el < CALIB_SETTLE_MS) {
                    target_cnt = 1;
                } else {
                    target_cnt = 0;
                }

                if (target_cnt < s_calib_cnt) {
                    s_calib_cnt = target_cnt;
                    if (target_cnt == 2) {
                        uart_stm32_send_haptic_buzz(60U);
                        uart_stm32_send_haptic_vibrate(80U);
                        (void)ble_adc_send_text("CALIB:2\n");
                    } else if (target_cnt == 1) {
                        uart_stm32_send_haptic_buzz(60U);
                        uart_stm32_send_haptic_vibrate(80U);
                        (void)ble_adc_send_text("CALIB:1\n");
                    } else {
                        uart_stm32_send_haptic_buzz(200U);
                        uart_stm32_send_haptic_vibrate(350U);
                        (void)ble_adc_send_text("CALIB:HOLD\n");
                    }
                }
            }

            /* Periodic OLED update (200ms): gesture name + countdown + live ADC.
             * state = phase * 4 + countdown_step (0-19, maps to 5 gestures × 4 steps). */
            if ((now - s_calib_oled_ts) >= 200U) {
                s_calib_oled_ts = now;
                uint8_t phase_n = calibration_current_phase();
                uint8_t step_n  = (s_calib_cnt == 3) ? 0U :
                                  (s_calib_cnt == 2) ? 1U :
                                  (s_calib_cnt == 1) ? 2U : 3U;
                uart_stm32_send_calib_adc((uint8_t)(phase_n * 4U + step_n), s_last_adc);
            }

            uint16_t new_profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS];
            int rc = calibration_update(s_last_adc, new_profiles);

            if (rc >= 1 && rc <= (int)CALIB_NUM_GESTURES) {
                /* One phase completed — play OK sound, next phase starts automatically. */
                uart_stm32_send_haptic_buzz(120U);
                uart_stm32_send_haptic_vibrate(180U);
                (void)ble_adc_send_text("CALIB: phase OK\n");
                LOG_INF("Calibration phase %d done, starting next", rc);
            } else if (rc == 10) {
                /* All 5 gestures calibrated — extract per-finger values and apply. */
                uint16_t open_p[4], fist_p[4];
                for (uint8_t i = 0U; i < 4U; i++) {
                    open_p[i] = new_profiles[0][i]; /* NONE: extended */
                }
                fist_p[0] = new_profiles[1][0]; /* WATER: index  bent */
                fist_p[1] = new_profiles[2][1]; /* WC:    middle bent */
                fist_p[2] = new_profiles[3][2]; /* FOOD:  ring   bent */
                fist_p[3] = new_profiles[4][3]; /* HELP:  pinky  bent */
                gesture_set_profiles(open_p, fist_p);
                int save_rc = calibration_save(new_profiles);
                if (save_rc != 0) {
                    LOG_ERR("NVS save failed: %d — profiles applied but not persisted", save_rc);
                    uart_stm32_send_oled_text("SAVERR");
                    (void)ble_adc_send_text("CALIB:SAVERR\n");
                } else {
                    uart_stm32_send_oled_text("SAVED!");
                    (void)ble_adc_send_text("CALIB:saved\n");
                    LOG_INF("Calibration complete: all gestures saved to NVS");
                }
                uart_stm32_send_haptic_buzz(300U);
                uart_stm32_send_haptic_vibrate(400U);
                (void)ble_adc_send_text("CALIB:done\n");
                s_calib_cnt = -1;
            }
        }

        if (uart_stm32_poll_byte(&b) != 0) {
            k_usleep(200U);
            continue;
        }

        s_total_bytes++;
        if (raw_log_count < RAW_LOG_COUNT) {
            LOG_INF("rx[%u]=0x%02x", raw_log_count, b);
            raw_log_count++;
        }

        frame_hmac_t hf;
        int rc = frame_hmac_parser_push_byte(&s_uart_parser, b, &hf);

        if (rc == 1) {
            handle_frame(&hf.base);
        } else if (rc == -2) {
            s_bad_frames++;
        }
    }

    return 0;
}
