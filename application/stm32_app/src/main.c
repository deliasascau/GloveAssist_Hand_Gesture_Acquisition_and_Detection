/*
 * GloveAssist STM32 — sensor hub main entry point.
 *
 * Role: sensor hub only (ESP32 = brain, STM32 = sensor hub).
 *
 *   PA0..PA3  -> ADC1 DMA circular -> 4 flex sensor values
 *   PA9/PA10  -> USART1 TX/RX <-> ESP32 UART2 (GPIO16/GPIO17)
 *   PC13      -> Built-in LED (active LOW)
 *   PB10/PB11 -> I2C2 SCL/SDA -> SSD1306 OLED 0x3C
 *   PB4       -> TIM3_CH1 PWM -> Passive buzzer (transistor driver)
 *   PB6       -> TIM4_CH1 PWM -> Motor vibration (ULN2003)
 *
 * All actuator logic lives in haptic.c.
 * All UART framing lives in uart_esp32.c.
 * All ADC/DMA init lives in adc_sensor.c.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <stdio.h>
#include <string.h>

#include "frame_protocol.h"
#include "adc_sensor.h"
#include "haptic.h"
#include "uart_esp32.h"
#include "alert.h"

/* -- Timing ------------------------------------------------------------ */
#define TX_PERIOD_MS      20U     /* sensor frame TX period (50 Hz)   */
#define HB_PERIOD_MS      500U    /* heartbeat TX period               */
#define LINK_TIMEOUT_MS  3000U    /* no ACK for 3 s -> local alert */
#define LINK_BOOT_GRACE_MS 6000U  /* wait for ESP32 session after reset */
#define ALERT_TIMEOUT_MS 10000U   /* re-raise ALERT if no ACK in 10 s */
#define DIAG_PERIOD_MS   10000U   /* OLED RX diagnostics period       */
#define FAULT_STATUS_PERIOD_MS 1000U /* report active sensor faults     */
#define WDT_TIMEOUT_MS   2000U    /* FR-009: hardware reset after 2 s */
#define WDT_FEED_MS       500U    /* feed only after the health loop  */

/* Raise local audible/visual alert when the ESP32 link is lost. */
#define LOCAL_ALERT_FALLBACK_ENABLED 1

/* -- Link state -------------------------------------------------------- */
typedef enum {
    LINK_NORMAL,
    LINK_UART_LOST,
    LINK_ALERT_PENDING,
} link_state_t;

/* ── Module-level state ───────────────────────────────────────────────── */
static uint8_t      s_seq;
static uint8_t      s_hb_seq;
static uint8_t      s_status_seq;
static uint32_t     s_last_sensor_tx;
static uint32_t     s_last_hb_tx;
static uint32_t     s_last_ack_time;
static uint32_t     s_boot_time;
#if LOCAL_ALERT_FALLBACK_ENABLED
static uint32_t     s_alert_raised_at;
#endif
static uint32_t     s_last_diag_tx;
static uint32_t     s_last_fault_status_tx;
static uint32_t     s_last_wdt_feed;
static link_state_t s_link_state = LINK_NORMAL;
static adc_sensor_fault_status_t s_adc_fault;
static const struct device *const s_wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int s_wdt_channel_id = -1;

/* ── Frame TX helpers ─────────────────────────────────────────────────── */
static void send_sensor_frame(const uint16_t adc[ADC_NUM_CHANNELS])
{
    uint8_t      payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_hmac_t frame;

    frame_make_sensor_payload(payload, adc[0], adc[1], adc[2], adc[3]);
    if (frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_SENSOR_RAW, s_seq++, payload) == 0) {
        uart_esp32_send_hmac_frame(&frame);
    }
}

static void send_heartbeat(void)
{
    uint8_t      payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_hmac_t frame;

    if (frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_HEARTBEAT, s_hb_seq++, payload) == 0) {
        uart_esp32_send_hmac_frame(&frame);
    }
}

static void send_sensor_fault_status(void)
{
    uint8_t      payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_hmac_t frame;

    payload[0] = (uint8_t)STATUS_ERR;
    payload[1] = s_adc_fault.active_mask;
    payload[2] = s_adc_fault.low_mask;
    payload[3] = s_adc_fault.high_mask;

    if (frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_STATUS, s_status_seq++, payload) == 0) {
        uart_esp32_send_hmac_frame(&frame);
    }
}

static void format_sensor_fault_line(char *buf, size_t len)
{
    size_t pos = 0U;
    bool has_low;
    bool has_high;
    int n;

    if ((buf == NULL) || (len == 0U)) {
        return;
    }

    n = snprintf(buf, len, "S");
    if (n > 0) {
        pos = (size_t)n;
    }

    for (uint8_t i = 0U; i < ADC_NUM_CHANNELS; i++) {
        if ((s_adc_fault.active_mask & (uint8_t)(1U << i)) != 0U) {
            if (pos < len) {
                n = snprintf(&buf[pos], len - pos, "%u", (unsigned)(i + 1U));
                if (n > 0) {
                    pos += (size_t)n;
                }
            }
        }
    }

    has_low = (s_adc_fault.low_mask != 0U);
    has_high = (s_adc_fault.high_mask != 0U);
    if (pos < len) {
        if (has_low && has_high) {
            (void)snprintf(&buf[pos], len - pos, " MIX");
        } else if (has_low) {
            (void)snprintf(&buf[pos], len - pos, " LOW");
        } else if (has_high) {
            (void)snprintf(&buf[pos], len - pos, " HIGH");
        }
    }
}

static void show_sensor_fault(void)
{
    char line[16];

    format_sensor_fault_line(line, sizeof(line));
    haptic_post_oled_show("ADC FAULT", line);
}

/* -- Secure UART session handshake ------------------------------------ */
static void tick_session(uint32_t now)
{
    uint32_t nonce;

    if (!uart_esp32_poll_session_hello(&nonce)) {
        return;
    }

    frame_secure_set_session(nonce);
    s_seq            = 0U;
    s_hb_seq         = 0U;
    s_status_seq     = 0U;
    s_link_state     = LINK_NORMAL;
    s_last_sensor_tx = now;
    s_last_hb_tx     = now;
    s_last_ack_time  = now;
    s_last_fault_status_tx = now - FAULT_STATUS_PERIOD_MS;

    alert_clear();
    haptic_led_set(false);
    if (s_adc_fault.active_mask != 0U) {
        show_sensor_fault();
    } else {
        haptic_post_oled_show("GloveAssist", "Activ");
    }
    uart_esp32_send_session_ack(nonce);
}

/* -- CMD dispatcher: enqueue actuator work, never block the main loop. -- */
static void handle_cmd(const frame_t *f)
{
    switch ((cmd_type_t)f->payload[0]) {

    case CMD_CAREGIVER_ACK:
        /* payload[1] carries the gesture_id that the caregiver is responding to */
        haptic_post_caregiver_ack(f->payload[1]);
        break;

    case CMD_HAPTIC_BUZZ: {
        uint32_t dur = (uint32_t)f->payload[1] * 10U;
        haptic_post_buzzer_beep(1U, (dur > 0U) ? dur : 100U, 0U);
        break;
    }

    case CMD_HAPTIC_VIBRATE: {
        uint32_t dur = (uint32_t)f->payload[1] * 10U;
        haptic_post_motor_pulse(75U, (dur > 0U) ? dur : 150U);
        break;
    }

    case CMD_OLED_TEXT: {
        char text[9] = {0};
        (void)memcpy(text, &f->payload[2], 6U);
        haptic_post_oled_show(text, "GloveAssist");
        break;
    }

    case CMD_GESTURE_FEEDBACK: {
        haptic_post_gesture_feedback(f->payload[1]);
        break;
    }

    case CMD_LED_ACK: {
        uint8_t n = (f->payload[1] > 0U) ? f->payload[1] : 1U;
        haptic_post_led_ack(n);
        break;
    }

    case CMD_OLED_CALIB:
        haptic_post_oled_calib_show(f->payload[1], f->payload[2],
                                    f->payload[3], f->payload[4],
                                    f->payload[5]);
        break;

    case CMD_CALIBRATE:
        haptic_post_oled_show("Calibrare...", "");
        break;

    default:
        break;
    }
}

/* ── Link watchdog helpers ────────────────────────────────────────────── */

static link_state_t handle_uart_lost(link_state_t state,
                                     uint32_t     now)
{
#if !LOCAL_ALERT_FALLBACK_ENABLED
    ARG_UNUSED(state);
    ARG_UNUSED(now);
    alert_clear();
    haptic_led_set(false);
    return LINK_UART_LOST;
#else
    if (state == LINK_ALERT_PENDING) {
        if (alert_ack_received()) {
            alert_clear();
            haptic_post_oled_show("ALERTA", "Logica lipsa");
            haptic_post_buzzer_beep(2U, 80U, 80U);
            return LINK_UART_LOST;
        }
        if ((now - s_alert_raised_at) >= ALERT_TIMEOUT_MS) {
            alert_clear();
            return LINK_UART_LOST;
        }
        return LINK_ALERT_PENDING;
    }

    /* In local fallback (ESP32 missing), do not classify gestures on STM32.
     * Keep only a fixed missing-logic alert state on OLED. */
    alert_raise();
    s_alert_raised_at = now;
    haptic_post_oled_show("ALERTA", "Logica lipsa");
    haptic_post_buzzer_beep(2U, 120U, 120U);
    haptic_post_motor_pulse(80U, 250U);
    return LINK_ALERT_PENDING;
#endif
}

static void tick_sensor_tx(uint32_t now, const uint16_t adc[ADC_NUM_CHANNELS])
{
    if (!frame_secure_session_ready()
        || (s_link_state != LINK_NORMAL)
        || (s_adc_fault.active_mask != 0U)
        || ((now - s_last_sensor_tx) < TX_PERIOD_MS)) {
        return;
    }
    s_last_sensor_tx = now;
    send_sensor_frame(adc);
}

static void tick_sensor_fault(uint32_t now, const uint16_t adc[ADC_NUM_CHANNELS])
{
    bool changed = adc_sensor_fault_update(adc, &s_adc_fault);

    if (changed) {
        if (s_adc_fault.active_mask != 0U) {
            show_sensor_fault();
            haptic_post_buzzer_beep(3U, 60U, 60U);
            haptic_post_motor_pulse(80U, 250U);
            s_last_fault_status_tx = now - FAULT_STATUS_PERIOD_MS;
        } else {
            haptic_post_oled_show("SENSOR OK", "Reluat");
            haptic_post_buzzer_beep(1U, 90U, 0U);
            if (frame_secure_session_ready() && (s_link_state == LINK_NORMAL)) {
                send_sensor_fault_status();
            }
            s_last_sensor_tx = now;
        }
    }

    if ((s_adc_fault.active_mask != 0U)
        && frame_secure_session_ready()
        && (s_link_state == LINK_NORMAL)
        && ((now - s_last_fault_status_tx) >= FAULT_STATUS_PERIOD_MS)) {
        s_last_fault_status_tx = now;
        show_sensor_fault();
        send_sensor_fault_status();
    }
}

static void tick_heartbeat(uint32_t now)
{
    if (!frame_secure_session_ready()
        || (s_link_state != LINK_NORMAL)
        || ((now - s_last_hb_tx) < HB_PERIOD_MS)) {
        return;
    }
    s_last_hb_tx = now;
    send_heartbeat();
}

static void tick_link_watchdog(uint32_t now, const uint16_t adc[ADC_NUM_CHANNELS])
{
    ARG_UNUSED(adc);

    if (!frame_secure_session_ready()) {
        if ((now - s_boot_time) < LINK_BOOT_GRACE_MS) {
            return;
        }
        if (s_link_state == LINK_NORMAL) {
            s_link_state = LINK_UART_LOST;
            haptic_post_oled_show("Astept ESP32", "Mod local");
            haptic_post_buzzer_beep(2U, 200U, 100U);
        }
        s_link_state = handle_uart_lost(s_link_state, now);
#if LOCAL_ALERT_FALLBACK_ENABLED
        haptic_led_set(((now % 200U) < 100U));  /* fast blink */
#endif
        return;
    }

    if (uart_esp32_poll_ack()) {
        s_last_ack_time = now;
        if (s_link_state != LINK_NORMAL) {
            s_link_state = LINK_NORMAL;
            alert_clear();
            haptic_led_set(false);
            if (s_adc_fault.active_mask != 0U) {
                show_sensor_fault();
            } else {
                haptic_post_oled_show("GloveAssist", "Activ");
            }
            haptic_post_buzzer_beep(1U, 100U, 0U);
        }
        haptic_led_toggle();
        return;
    }

    if ((s_link_state == LINK_NORMAL)
        && ((now - s_last_ack_time) >= LINK_TIMEOUT_MS)) {
        s_link_state = LINK_UART_LOST;
#if LOCAL_ALERT_FALLBACK_ENABLED
        haptic_post_oled_show("Link pierdut!", "Mod local");
        haptic_post_buzzer_beep(2U, 200U, 100U);
#else
        alert_clear();
        haptic_led_set(false);
        if (s_adc_fault.active_mask != 0U) {
            show_sensor_fault();
        } else {
            haptic_post_oled_show("GloveAssist", "Astept ESP32");
        }
#endif
    }

    if (s_link_state != LINK_NORMAL) {
        s_link_state = handle_uart_lost(s_link_state, now);
#if LOCAL_ALERT_FALLBACK_ENABLED
        haptic_led_set(((now % 200U) < 100U));  /* fast blink */
#endif
    }
}

static void tick_cmd(void)
{
    if (uart_esp32_poll_bad_cmd()) {
        /* Auth/SOF failures are UART/security diagnostics. Do not overwrite
         * the patient-facing OLED while the valid gesture path is healthy. */
        (void)0;
    }

    frame_t cmd;
    if (!uart_esp32_poll_cmd(&cmd)) {
        return;
    }
    handle_cmd(&cmd);
}

/* ── Boot sequence ────────────────────────────────────────────────────── */

/* -- Hardware watchdog (STM32 IWDG) ----------------------------------- */
static void safety_watchdog_init(void)
{
    struct wdt_timeout_cfg cfg = {
        .flags = WDT_FLAG_RESET_SOC,
        .window = {
            .min = 0U,
            .max = WDT_TIMEOUT_MS,
        },
        .callback = NULL,
    };
    int rc;

    if (!device_is_ready(s_wdt)) {
        haptic_oled_show("WDT ERROR", "not ready");
        return;
    }

    s_wdt_channel_id = wdt_install_timeout(s_wdt, &cfg);
    if (s_wdt_channel_id < 0) {
        haptic_oled_show("WDT ERROR", "install");
        return;
    }

    rc = wdt_setup(s_wdt, 0U);
    if (rc < 0) {
        s_wdt_channel_id = -1;
        haptic_oled_show("WDT ERROR", "setup");
        return;
    }

    s_last_wdt_feed = k_uptime_get_32();
    (void)wdt_feed(s_wdt, s_wdt_channel_id);
}

static void safety_watchdog_feed(uint32_t now)
{
    int rc;

    if ((s_wdt_channel_id < 0)
        || ((now - s_last_wdt_feed) < WDT_FEED_MS)) {
        return;
    }

    rc = wdt_feed(s_wdt, s_wdt_channel_id);
    if (rc == 0) {
        s_last_wdt_feed = now;
    }
}

/* -- Boot sequence ----------------------------------------------------- */
static void boot_sequence(void)
{
    for (uint8_t i = 0U; i < 3U; i++) {
        haptic_led_set(true);  k_msleep(100U);
        haptic_led_set(false); k_msleep(100U);
    }
    haptic_boot_melody();
    haptic_oled_show("GloveAssist", "Activ");
}

/* ── Entry point ──────────────────────────────────────────────────────── */

int main(void)
{
    uart_esp32_init();
    adc_sensor_init();
    adc_sensor_fault_reset();
    haptic_init();
    alert_init();
    boot_sequence();
    safety_watchdog_init();

    s_boot_time      = k_uptime_get_32();
    s_last_sensor_tx = s_boot_time;
    s_last_hb_tx     = s_boot_time;
    s_last_ack_time  = s_boot_time;
    s_last_diag_tx   = s_boot_time;
    s_last_fault_status_tx = s_boot_time;

    while (1) {
        uint16_t adc[ADC_NUM_CHANNELS];
        adc_sensor_read(adc);
        uint32_t now = k_uptime_get_32();

        tick_session(now);
        tick_sensor_fault(now, adc);
        tick_sensor_tx(now, adc);
        tick_heartbeat(now);
        tick_link_watchdog(now, adc);
        tick_cmd();

        /* Drain RX diagnostic counters every 10 s to prevent u8 wrap-around. */
        if ((now - s_last_diag_tx) >= DIAG_PERIOD_MS) {
            s_last_diag_tx = now;
            uint8_t cmds, bad, bad_cmd;
            uart_esp32_get_rx_diag(&cmds, &bad, &bad_cmd);
            (void)cmds; (void)bad; (void)bad_cmd;
        }

        if ((s_link_state == LINK_NORMAL) && (s_adc_fault.active_mask == 0U)) {
            haptic_oled_idle_tick();
        }

        safety_watchdog_feed(now);
        k_msleep(1U);
    }
}
