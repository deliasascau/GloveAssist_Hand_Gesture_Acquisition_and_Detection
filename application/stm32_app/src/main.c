/*
 * GloveAssist STM32 — sensor hub main entry point.
 *
 * Role: sensor hub only (ESP32 = brain, STM32 = sensor hub).
 *
 *   PA0..PA3  -> ADC1 DMA circular -> 4 flex sensor values
 *   PA9/PA10  -> USART1 TX/RX <-> ESP32 UART2 (GPIO16/GPIO17)
 *   PC13      -> Built-in LED (active LOW)
 *   PB10/PB11 -> I2C2 SCL/SDA -> SSD1306 OLED 0x3C
 *   PB4       -> Active buzzer (GPIO HIGH)
 *   PB6       -> TIM4_CH1 PWM -> Motor vibration (ULN2003)
 *
 * All actuator logic lives in haptic.c.
 * All UART framing lives in uart_esp32.c.
 * All ADC/DMA init lives in adc_sensor.c.
 */

#include <zephyr/kernel.h>
#include <string.h>

#include "frame_protocol.h"
#include "adc_sensor.h"
#include "haptic.h"
#include "uart_esp32.h"
#include "alert.h"

/* -- Timing ------------------------------------------------------------ */
#define TX_PERIOD_MS      20U     /* sensor frame TX period (50 Hz)   */
#define HB_PERIOD_MS      500U    /* heartbeat TX period               */
#define LINK_TIMEOUT_MS  4000U    /* no ACK for 4 s -> link lost      */
#define ALERT_TIMEOUT_MS 10000U   /* re-raise ALERT if no ACK in 10 s */
#define DIAG_PERIOD_MS   10000U   /* OLED RX diagnostics period       */

/* -- Link state -------------------------------------------------------- */
typedef enum {
    LINK_NORMAL,
    LINK_UART_LOST,
    LINK_ALERT_PENDING,
} link_state_t;

/* ── Module-level state ───────────────────────────────────────────────── */
static uint8_t      s_seq;
static uint8_t      s_hb_seq;
static uint32_t     s_last_sensor_tx;
static uint32_t     s_last_hb_tx;
static uint32_t     s_last_ack_time;
static uint32_t     s_alert_raised_at;
static uint32_t     s_last_diag_tx;
static link_state_t s_link_state = LINK_NORMAL;

/* ── Frame TX helpers ─────────────────────────────────────────────────── */
static void send_sensor_frame(const uint16_t adc[ADC_NUM_CHANNELS])
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_t frame;

    frame_make_sensor_payload(payload, adc[0], adc[1], adc[2], adc[3]);
    if (frame_build(&frame, (uint8_t)FRAME_TYPE_SENSOR_RAW, s_seq++, payload) == 0) {
        uart_esp32_send_frame(&frame);
    }
}

static void send_heartbeat(void)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_t frame;

    if (frame_build(&frame, (uint8_t)FRAME_TYPE_HEARTBEAT, s_hb_seq++, payload) == 0) {
        uart_esp32_send_frame(&frame);
    }
}

/* -- CMD dispatcher (runs in main loop � k_msleep is safe here) ------- */
static void handle_cmd(const frame_t *f)
{
    switch ((cmd_type_t)f->payload[0]) {

    case CMD_CAREGIVER_ACK:
        /* payload[1] carries the gesture_id that the caregiver is responding to */
        haptic_caregiver_ack(f->payload[1]);
        break;

    case CMD_HAPTIC_BUZZ: {
        uint32_t dur = (uint32_t)f->payload[1] * 10U;
        haptic_buzzer_beep(1U, (dur > 0U) ? dur : 100U, 0U);
        break;
    }

    case CMD_HAPTIC_VIBRATE: {
        uint32_t dur = (uint32_t)f->payload[1] * 10U;
        haptic_motor_pulse(75U, (dur > 0U) ? dur : 150U);
        break;
    }

    case CMD_OLED_TEXT: {
        char text[9] = {0};
        (void)memcpy(text, &f->payload[2], 6U);
        haptic_oled_show(text, "GloveAssist");
        break;
    }

    case CMD_GESTURE_FEEDBACK: {
        /* Play unique per-gesture haptic pattern + OLED display. */
        haptic_gesture_feedback(f->payload[1]);
        break;
    }

    case CMD_LED_ACK: {
        uint8_t n = (f->payload[1] > 0U) ? f->payload[1] : 1U;
        for (uint8_t i = 0U; i < n; i++) {
            haptic_led_set(true);
            k_msleep(80U);
            haptic_led_set(false);
            k_msleep(80U);
        }
        break;
    }

    case CMD_OLED_CALIB:
        haptic_oled_calib_show(f->payload[1], f->payload[2],
                               f->payload[3], f->payload[4], f->payload[5]);
        break;

    case CMD_CALIBRATE:
        haptic_oled_show("Calibrare...", "");
        break;

    default:
        break;
    }
}

/* ── Link watchdog helpers ────────────────────────────────────────────── */

static link_state_t handle_uart_lost(const uint16_t adc[ADC_NUM_CHANNELS],
                                     link_state_t   state,
                                     uint32_t       now)
{
    if (state == LINK_ALERT_PENDING) {
        if (alert_ack_received()) {
            alert_clear();
            haptic_oled_show("Alerta OK!", "ESP32 ACK");
            haptic_buzzer_beep(2U, 80U, 80U);
            return LINK_UART_LOST;
        }
        if ((now - s_alert_raised_at) >= ALERT_TIMEOUT_MS) {
            alert_clear();
            return LINK_UART_LOST;
        }
        return LINK_ALERT_PENDING;
    }

    if (alert_is_help_gesture(adc)) {
        alert_raise();
        s_alert_raised_at = now;
        haptic_oled_show("HELP detectat", "Trimit alert!");
        haptic_buzzer_beep(3U, 100U, 100U);
        haptic_motor_pulse(100U, 500U);
        return LINK_ALERT_PENDING;
    }

    return LINK_UART_LOST;
}

static void tick_sensor_tx(uint32_t now, const uint16_t adc[ADC_NUM_CHANNELS])
{
    if ((s_link_state != LINK_NORMAL)
        || ((now - s_last_sensor_tx) < TX_PERIOD_MS)) {
        return;
    }
    s_last_sensor_tx = now;
    send_sensor_frame(adc);
}

static void tick_heartbeat(uint32_t now)
{
    if ((s_link_state != LINK_NORMAL)
        || ((now - s_last_hb_tx) < HB_PERIOD_MS)) {
        return;
    }
    s_last_hb_tx = now;
    send_heartbeat();
}

static void tick_link_watchdog(uint32_t now, const uint16_t adc[ADC_NUM_CHANNELS])
{
    if (uart_esp32_poll_ack()) {
        s_last_ack_time = now;
        if (s_link_state != LINK_NORMAL) {
            s_link_state = LINK_NORMAL;
            alert_clear();
            haptic_led_set(false);
            haptic_oled_show("GloveAssist", "Activ");
            haptic_buzzer_beep(1U, 100U, 0U);
        }
        haptic_led_toggle();
        return;
    }

    if ((s_link_state == LINK_NORMAL)
        && ((now - s_last_ack_time) >= LINK_TIMEOUT_MS)) {
        s_link_state = LINK_UART_LOST;
        haptic_oled_show("Link pierdut!", "Mod local");
        haptic_buzzer_beep(2U, 200U, 100U);
    }

    if (s_link_state != LINK_NORMAL) {
        s_link_state = handle_uart_lost(adc, s_link_state, now);
        haptic_led_set(((now % 200U) < 100U));  /* fast blink */
    }
}

static void tick_cmd(void)
{
    frame_t cmd;
    if (!uart_esp32_poll_cmd(&cmd)) {
        return;
    }
    handle_cmd(&cmd);
}

/* ── Boot sequence ────────────────────────────────────────────────────── */

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
    haptic_init();
    alert_init();
    boot_sequence();

    s_last_sensor_tx = k_uptime_get_32();
    s_last_hb_tx     = k_uptime_get_32();
    s_last_ack_time  = k_uptime_get_32();
    s_last_diag_tx   = k_uptime_get_32();

    while (1) {
        uint16_t adc[ADC_NUM_CHANNELS];
        adc_sensor_read(adc);
        uint32_t now = k_uptime_get_32();

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

        if (s_link_state == LINK_NORMAL) {
            haptic_oled_idle_tick();
        }

        k_msleep(1U);
    }
}
