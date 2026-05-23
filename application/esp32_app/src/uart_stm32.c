/*
 * GloveAssist - STM32 UART Communication Layer
 *
 * Owns the UART2 device (GPIO17 TX / GPIO16 RX, 115200 baud).
 * Provides frame TX helpers used by main and ble_adc modules.
 *
 * Wiring:
 *   ESP32 GPIO16 (RX) <- STM32 PA9  (TX)
 *   ESP32 GPIO17 (TX) -> STM32 PA10 (RX)
 */

#include "uart_stm32.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "frame_protocol.h"

LOG_MODULE_REGISTER(uart_stm32, LOG_LEVEL_INF);

static const struct device *s_uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart2));

/* Internal TX sequence counters (not visible outside this module). */
static uint8_t s_hb_seq;
static uint8_t s_cmd_seq;

/* ── Private helpers ──────────────────────────────────────────────────── */

static void send_frame(const frame_t *f)
{
    const uint8_t *p = (const uint8_t *)f;

    for (size_t i = 0U; i < sizeof(frame_t); i++) {
        uart_poll_out(s_uart_dev, p[i]);
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

int uart_stm32_init(void)
{
    if (!device_is_ready(s_uart_dev)) {
        LOG_ERR("UART2 not ready");
        return -1;
    }

    return 0;
}

int uart_stm32_poll_byte(uint8_t *b)
{
    return uart_poll_in(s_uart_dev, b);
}

void uart_stm32_send_ack(uint8_t seq)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;

    frame_make_status_payload(payload, (uint8_t)STATUS_ACK, seq);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_STATUS, seq, payload) == 0) {
        send_frame(&f);
    }
}

void uart_stm32_send_heartbeat(void)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_t f;

    if (frame_build(&f, (uint8_t)FRAME_TYPE_HEARTBEAT, s_hb_seq++, payload) == 0) {
        send_frame(&f);
    }
}

void uart_stm32_send_caregiver_ack(uint8_t gesture_id)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;

    frame_make_cmd_payload(payload, (uint8_t)CMD_CAREGIVER_ACK, gesture_id, NULL);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
        LOG_INF("Caregiver ACK -> STM32 (gesture %u)", (unsigned)gesture_id);
    }
}

void uart_stm32_send_oled_text(const char *text)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;

    /* CMD_OLED_TEXT: payload[0]=cmd, payload[1]=arg0, payload[2..7]=6-char text */
    frame_make_cmd_payload(payload, (uint8_t)CMD_OLED_TEXT, 0U, text);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
        LOG_INF("OLED text -> STM32: '%.6s'", text != NULL ? text : "");
    }
}

void uart_stm32_send_haptic_buzz(uint32_t duration_ms)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;
    /* STM32 handler multiplies payload[1] by 10 to get ms */
    uint8_t arg = (uint8_t)((duration_ms / 10U) > 255U ? 255U : (duration_ms / 10U));

    frame_make_cmd_payload(payload, (uint8_t)CMD_HAPTIC_BUZZ, arg, NULL);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
    }
}

void uart_stm32_send_haptic_vibrate(uint32_t duration_ms)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;
    uint8_t arg = (uint8_t)((duration_ms / 10U) > 255U ? 255U : (duration_ms / 10U));

    frame_make_cmd_payload(payload, (uint8_t)CMD_HAPTIC_VIBRATE, arg, NULL);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
    }
}

void uart_stm32_send_calibrate_cmd(void)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;

    frame_make_cmd_payload(payload, (uint8_t)CMD_CALIBRATE, 0U, NULL);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
        LOG_INF("CMD_CALIBRATE -> STM32");
    }
}

void uart_stm32_send_gesture_feedback(uint8_t gesture_id)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    frame_t f;

    frame_make_cmd_payload(payload, (uint8_t)CMD_GESTURE_FEEDBACK, gesture_id, NULL);
    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
        LOG_INF("Gesture feedback -> STM32 (gesture %u)", (unsigned)gesture_id);
    }
}

void uart_stm32_send_calib_adc(uint8_t state, const uint16_t adc[4])
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_t f;

    payload[0] = (uint8_t)CMD_OLED_CALIB;
    payload[1] = state;
    payload[2] = (uint8_t)(adc[0] >> 4U);
    payload[3] = (uint8_t)(adc[1] >> 4U);
    payload[4] = (uint8_t)(adc[2] >> 4U);
    payload[5] = (uint8_t)(adc[3] >> 4U);

    if (frame_build(&f, (uint8_t)FRAME_TYPE_COMMAND, s_cmd_seq++, payload) == 0) {
        send_frame(&f);
    }
}
