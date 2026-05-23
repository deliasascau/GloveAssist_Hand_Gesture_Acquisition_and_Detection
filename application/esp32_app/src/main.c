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
 *   - Classifies gesture with 500ms stability filter
 *   - Notifies BLE phone with gesture name + raw ADC values
 *   - Sends HEARTBEAT to STM32 every 1000ms
 *   - On BLE caregiver "OK" -> uart_stm32_send_caregiver_ack()
 *     -> STM32 plays OLED + buzzer + motor feedback
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <stdio.h>
#include <string.h>

#if defined(CONFIG_IMG_MANAGER)
#include <zephyr/dfu/mcuboot.h>
#endif

#include "frame_protocol.h"
#include "gesture.h"
#include "uart_stm32.h"
#include "ble_adc.h"
#include "gpio_alert.h"
#include "calibration.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(esp32_main, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD_MS   1000U  /* STM32 heartbeat TX period */
#define STATUS_REPORT_MS      3000U  /* periodic link-stats log interval */
#define WIFI_MQTT_PERIOD_MS   1000U  /* MQTT keepalive drive interval */
#define RAW_LOG_COUNT           16U  /* first N raw bytes logged at boot */
#define ADC_BLE_PERIOD_MS     5000U  /* ADC BLE notify period (avoids flooding gesture notifs) */
#define GESTURE_RETRIGGER_MS  5000U  /* same gesture may re-trigger after this cooldown */
#define GESTURE_COMPOSE_COUNT    5U  /* consecutive identical frames before debounce (100ms at 50Hz) */
#define RECAL_HOLD_MS        20000U  /* full-fist hold duration triggering recalibration */

/* ── Link statistics ────────────────────────────────────────────────────────────────── */
static uint32_t s_total_frames;
static uint32_t s_bad_frames;
static uint32_t s_total_bytes;
static uint32_t s_seq_gaps;
static uint8_t  s_last_seq = 0xFFU;  /* 0xFF = no frame received yet */

/* ── Gesture stability filter ─────────────────────────────────────────────────────── */
static gesture_id_t s_last_stable_gesture;
static gesture_id_t s_pending_gesture;
static uint32_t     s_gesture_stable_since;
static uint32_t     s_last_stable_time;
static bool         s_midpoint_buzzed;

/* Compose window: GESTURE_COMPOSE_COUNT identical frames before debounce */
static gesture_id_t s_compose_gesture = GESTURE_NONE;
static uint8_t      s_compose_count   = 0U;

/* ── Fist-hold recalibration + first-boot flags ──────────────────────────────────── */
static uint32_t s_fist_hold_start  = 0U;
static bool     s_recal_requested  = false;
static bool     s_need_calibration = false;

/* ── ADC snapshot + periodic timer baselines ─────────────────────────────────────── */
static uint16_t s_last_adc[4];
static uint32_t s_last_hb_tx;
static uint32_t s_last_report;
static uint32_t s_last_adc_tx;
static uint32_t s_last_mqtt_tx;

/* ── UART RX state ───────────────────────────────────────────────────────────────────── */
static frame_parser_t s_parser;
static uint8_t        s_raw_log_count;

/* ── Calibration UI state ───────────────────────────────────────────────────────────── */
static const char *s_last_phase_str;
static uint32_t    s_calib_phase_ts;
static uint32_t    s_calib_oled_ts;
static int8_t      s_calib_cnt = -1;

/* ── Sensor frame processing ─────────────────────────────────────────────────────── */

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

/* Track full-fist hold for auto-recalibration trigger. */
static void track_fist_hold(gesture_id_t g, uint32_t now)
{
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
        s_fist_hold_start = 0U;
    }
}

/* Emit a confirmed stable gesture: BLE notify + MQTT + STM32 feedback. */
static void report_stable_gesture(gesture_id_t g,
                                   const uint16_t adc[4], uint32_t now)
{
    s_last_stable_gesture = g;
    s_last_stable_time    = now;
    LOG_INF("GESTURE: %s (id=%u)  adc %u %u %u %u",
            gesture_name(g), (unsigned)g,
            adc[0], adc[1], adc[2], adc[3]);
    ble_adc_set_last_gesture(g);
    (void)ble_adc_send_gesture(g, adc);
    (void)wifi_mqtt_publish_gesture(g);
    (void)wifi_mqtt_publish_adc(adc);
    uart_stm32_send_gesture_feedback((uint8_t)g);
}

/*
 * Compose window + stability/debounce filter.
 * Compose  : GESTURE_COMPOSE_COUNT identical frames required before timer starts.
 * Stability: gesture must hold stable for GESTURE_STABLE_MS before reporting.
 * Retrigger: same gesture may re-trigger after GESTURE_RETRIGGER_MS cooldown.
 */
static void run_stability_filter(gesture_id_t g,
                                  const uint16_t adc[4], uint32_t now)
{
    if (g != s_compose_gesture) {
        s_compose_gesture = g;
        s_compose_count   = 1U;
        return;
    }
    if (s_compose_count < GESTURE_COMPOSE_COUNT) {
        s_compose_count++;
        return;
    }

    if (g != s_pending_gesture) {
        s_pending_gesture      = g;
        s_gesture_stable_since = now;
        s_midpoint_buzzed      = false;
    } else if (g != GESTURE_NONE) {
        uint32_t stable_ms = now - s_gesture_stable_since;
        if (!s_midpoint_buzzed && (stable_ms >= (GESTURE_STABLE_MS / 2U))) {
            s_midpoint_buzzed = true;
            uart_stm32_send_haptic_buzz(30U);  /* progress buzz at 1s */
        }
    }

    if ((g != GESTURE_NONE)
        && (g == s_pending_gesture)
        && ((g != s_last_stable_gesture)
            || ((now - s_last_stable_time) >= GESTURE_RETRIGGER_MS))
        && ((now - s_gesture_stable_since) >= GESTURE_STABLE_MS)) {
        report_stable_gesture(g, adc, now);
    }
}

static void handle_sensor_frame(const frame_t *f)
{
    uint16_t     adc[4];
    uint32_t     now = k_uptime_get_32();

    frame_decode_sensor_payload(f->payload, &adc[0], &adc[1], &adc[2], &adc[3]);
    check_seq(f->seq);
    s_total_frames++;
    uart_stm32_send_ack(f->seq);

    s_last_adc[0] = adc[0];
    s_last_adc[1] = adc[1];
    s_last_adc[2] = adc[2];
    s_last_adc[3] = adc[3];

    gesture_id_t g = gesture_classify(adc);
    track_fist_hold(g, now);
    run_stability_filter(g, adc, now);
}

static void handle_frame(const frame_t *f)
{
    switch ((frame_type_t)f->type) {
    case FRAME_TYPE_SENSOR_RAW:
        handle_sensor_frame(f);
        break;
    case FRAME_TYPE_HEARTBEAT:
        uart_stm32_send_ack(f->seq);
        break;
    case FRAME_TYPE_STATUS:
        LOG_INF("STM32 status: 0x%02x", f->payload[0]);
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

/* ── Calibration helpers ──────────────────────────────────────────────────────────── */

/*
 * Extract per-finger open/fist values from a full profile matrix and apply
 * them to the hysteresis gesture classifier.
 *   profiles[0] = NONE  gesture (all fingers extended)  → open baseline
 *   profiles[1..4][finger matching gesture index] = bent baseline per finger
 */
static void apply_calibration_profiles(
        const uint16_t profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS])
{
    uint16_t open_p[4], fist_p[4];

    for (uint8_t i = 0U; i < 4U; i++) {
        open_p[i] = profiles[0][i]; /* NONE: all fingers extended */
    }
    fist_p[0] = profiles[1][0]; /* WATER: index  finger bent */
    fist_p[1] = profiles[2][1]; /* WC:    middle finger bent */
    fist_p[2] = profiles[3][2]; /* FOOD:  ring   finger bent */
    fist_p[3] = profiles[4][3]; /* HELP:  pinky  finger bent */
    gesture_set_profiles(open_p, fist_p);
}

static void load_gesture_profiles(void)
{
    uint16_t saved[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS];

    if (calibration_init(saved) != 0) {
        s_need_calibration = true;
        LOG_INF("First boot: no calibration data — will auto-calibrate");
    } else {
        apply_calibration_profiles(saved);
        LOG_INF("Gesture profiles loaded from NVS");
    }
}

static void start_calibration(void)
{
    uart_stm32_send_oled_text("CALIBR");
    uart_stm32_send_haptic_buzz(150U);
    uart_stm32_send_haptic_vibrate(200U);
    calibration_start();
    s_calib_cnt      = -1;
    s_last_phase_str = NULL;
    (void)ble_adc_send_text("CALIB: starting\n");
}

static void on_calib_phase_change(const char *phase_str, uint32_t now)
{
    char pmsg[20];

    s_last_phase_str = phase_str;
    s_calib_phase_ts = now;
    s_calib_cnt      = 3;
    (void)snprintf(pmsg, sizeof(pmsg), "P:%s\n", phase_str);
    (void)ble_adc_send_text(pmsg);
    uart_stm32_send_haptic_buzz(80U);
    uart_stm32_send_haptic_vibrate(100U);
    (void)ble_adc_send_text("CALIB:3\n");
}

static void tick_calibration_countdown(uint32_t now)
{
    uint32_t elapsed;
    int8_t   target;

    if (s_calib_cnt < 0) {
        return;
    }
    elapsed = now - s_calib_phase_ts;

    if (elapsed < (CALIB_SETTLE_MS / 3U)) {
        target = 3;
    } else if (elapsed < ((CALIB_SETTLE_MS * 2U) / 3U)) {
        target = 2;
    } else if (elapsed < CALIB_SETTLE_MS) {
        target = 1;
    } else {
        target = 0;
    }

    if (target >= s_calib_cnt) {
        return;
    }
    s_calib_cnt = target;

    if (target == 2) {
        uart_stm32_send_haptic_buzz(60U);
        uart_stm32_send_haptic_vibrate(80U);
        (void)ble_adc_send_text("CALIB:2\n");
    } else if (target == 1) {
        uart_stm32_send_haptic_buzz(60U);
        uart_stm32_send_haptic_vibrate(80U);
        (void)ble_adc_send_text("CALIB:1\n");
    } else {
        uart_stm32_send_haptic_buzz(200U);
        uart_stm32_send_haptic_vibrate(350U);
        (void)ble_adc_send_text("CALIB:HOLD\n");
    }
}

static void tick_calibration_oled(uint32_t now)
{
    if ((now - s_calib_oled_ts) < 200U) {
        return;
    }
    s_calib_oled_ts = now;

    uint8_t phase_n = calibration_current_phase();
    uint8_t step_n  = (s_calib_cnt == 3) ? 0U :
                      (s_calib_cnt == 2) ? 1U :
                      (s_calib_cnt == 1) ? 2U : 3U;
    uart_stm32_send_calib_adc((uint8_t)(phase_n * 4U + step_n), s_last_adc);
}

static void on_calib_complete(
        const uint16_t new_profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS])
{
    apply_calibration_profiles(new_profiles);

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

static void tick_calibration(uint32_t now)
{
    uint16_t    new_profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS];
    const char *phase_str;
    int         rc;

    if (!calibration_active()) {
        return;
    }

    phase_str = calibration_status_line();
    if (phase_str != s_last_phase_str) {
        on_calib_phase_change(phase_str, now);
    }
    tick_calibration_countdown(now);
    tick_calibration_oled(now);

    rc = calibration_update(s_last_adc, new_profiles);
    if (rc >= 1 && rc <= (int)CALIB_NUM_GESTURES) {
        uart_stm32_send_haptic_buzz(120U);
        uart_stm32_send_haptic_vibrate(180U);
        (void)ble_adc_send_text("CALIB: phase OK\n");
        LOG_INF("Calibration phase %d done, starting next", rc);
    } else if (rc == 10) {
        on_calib_complete(new_profiles);
    }
}

/* ── Periodic tick helpers ─────────────────────────────────────────────────────────── */

static void tick_heartbeat(uint32_t now)
{
    if ((now - s_last_hb_tx) < HEARTBEAT_PERIOD_MS) {
        return;
    }
    s_last_hb_tx = now;
    uart_stm32_send_heartbeat();
}

static void tick_adc_notify(uint32_t now)
{
    if ((now - s_last_adc_tx) < ADC_BLE_PERIOD_MS) {
        return;
    }
    s_last_adc_tx = now;
    if (s_total_frames > 0U) {
        (void)ble_adc_send_values(s_last_adc);
    }
}

static void tick_status_report(uint32_t now)
{
    if ((now - s_last_report) < STATUS_REPORT_MS) {
        return;
    }
    s_last_report = now;
    log_link_status();
}

static void tick_mqtt(uint32_t now)
{
    if ((now - s_last_mqtt_tx) < WIFI_MQTT_PERIOD_MS) {
        return;
    }
    s_last_mqtt_tx = now;
    wifi_mqtt_process();
}

/* ── Command / event handlers ──────────────────────────────────────────────────────── */

static void handle_ble_calibrate(void)
{
    if (!ble_adc_pop_calibration_request()) {
        return;
    }
    if (calibration_active()) {
        (void)ble_adc_send_text("CALIB: active\n");
    } else if (s_total_frames == 0U) {
        (void)ble_adc_send_text("CALIB: no sensor\n");
        LOG_WRN("Calibration requested but no STM32 frames yet");
    } else {
        LOG_INF("BLE: starting calibration");
        start_calibration();
    }
}

static void handle_auto_calibrate(void)
{
    if (!s_need_calibration || calibration_active() || (s_total_frames == 0U)) {
        return;
    }
    s_need_calibration = false;
    LOG_INF("Auto-starting calibration (first boot / no profiles)");
    start_calibration();
}

static void handle_recalibrate(void)
{
    if (!s_recal_requested || calibration_active()) {
        return;
    }
    s_recal_requested = false;
    LOG_INF("Fist 20s held -> starting recalibration");
    uart_stm32_send_oled_text("RECAL");  /* immediately overridden by start_calibration */
    start_calibration();
}

static void handle_caregiver_ack(void)
{
    gesture_id_t ack_g = ble_adc_pop_caregiver_ack_request();

    if (ack_g == GESTURE_NONE) {
        return;
    }
    uart_stm32_send_caregiver_ack((uint8_t)ack_g);
    (void)ble_adc_send_text("Understood\n");
    LOG_INF("Caregiver ACK: gesture %u -> STM32 + BLE", (unsigned)ack_g);
}

static void handle_ota_request(void)
{
    if (!ble_adc_pop_ota_request()) {
        return;
    }
#if defined(CONFIG_IMG_MANAGER)
    int ota_err = boot_request_upgrade(BOOT_UPGRADE_TEST);
    LOG_INF("OTA swap requested (err=%d) - rebooting", ota_err);
    k_msleep(200U);
    sys_reboot(SYS_REBOOT_COLD);
#else
    LOG_WRN("OTA requested but MCUboot IMG_MANAGER not enabled");
#endif
}

static void handle_gpio_alert(void)
{
    static const uint16_t zeroadc[4] = {0U};

    if (!gpio_alert_poll()) {
        return;
    }
    (void)ble_adc_send_gesture(GESTURE_HELP, zeroadc);
    LOG_WRN("Emergency HELP forwarded via BLE (GPIO alert path)");
}

static void process_uart_rx(void)
{
    uint8_t b;
    frame_t f;
    int     rc;
    uint8_t budget = 64U;  /* max bytes per call — prevents starvation of other tasks */

    while ((budget-- > 0U) && (uart_stm32_poll_byte(&b) == 0)) {
        s_total_bytes++;
        if (s_raw_log_count < RAW_LOG_COUNT) {
            LOG_INF("rx[%u]=0x%02x", s_raw_log_count, b);
            s_raw_log_count++;
        }
        rc = frame_parser_push_byte(&s_parser, b, &f);
        if (rc == 1) {
            handle_frame(&f);
        } else if (rc == -2) {
            s_bad_frames++;
        }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────────── */

int main(void)
{
    LOG_INF("=== GloveAssist ESP32 ===");

    if (uart_stm32_init() != 0) {
        LOG_ERR("UART STM32 init failed");
        return -1;
    }
    if (ble_adc_init() != 0) {
        LOG_ERR("BLE init failed");
        return -1;
    }
    if (gpio_alert_init() != 0) {
        LOG_WRN("GPIO alert init failed — emergency backup unavailable");
    }

    load_gesture_profiles();
    (void)wifi_mqtt_init();
    frame_parser_init(&s_parser);

    s_last_hb_tx = s_last_report = s_last_adc_tx = s_last_mqtt_tx =
        k_uptime_get_32();

    LOG_INF("Init OK. Waiting for STM32 frames on UART2...");

    while (1) {
        uint32_t now = k_uptime_get_32();

        tick_heartbeat(now);
        tick_adc_notify(now);
        tick_status_report(now);
        tick_mqtt(now);

        handle_ble_calibrate();
        handle_auto_calibrate();
        handle_recalibrate();
        handle_caregiver_ack();
        handle_ota_request();

        tick_calibration(now);
        handle_gpio_alert();
        process_uart_rx();

        k_usleep(200U);
    }

    return 0;
}
