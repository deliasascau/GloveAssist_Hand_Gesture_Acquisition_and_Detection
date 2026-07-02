#ifndef UART_STM32_H
#define UART_STM32_H

#include <stdint.h>
#include "frame_protocol.h"

/**
 * @brief Initialize UART2 device for STM32 communication.
 * @return 0 on success, negative errno on failure.
 */
int uart_stm32_init(void);

/**
 * @brief Poll one byte from STM32 UART (non-blocking).
 * @param b  Output byte.
 * @return 0 if byte available, non-zero if no byte.
 */
int uart_stm32_poll_byte(uint8_t *b);

/**
 * @brief Send HMAC-only SESSION_HELLO to STM32.
 */
void uart_stm32_send_session_hello(uint32_t session_nonce);

/**
 * @brief Send a STATUS_ACK frame to STM32 mirroring the received SEQ.
 * @param seq  Sequence number from the received frame.
 */
void uart_stm32_send_ack(uint8_t seq);

/**
 * @brief Send a HEARTBEAT frame to STM32 (sequence managed internally).
 */
void uart_stm32_send_heartbeat(void);

/**
 * @brief Send CMD_CAREGIVER_ACK to STM32 (triggers OLED+buzz+motor feedback).
 * @param gesture_id  The gesture_id_t that was confirmed by the caregiver.
 */
void uart_stm32_send_caregiver_ack(uint8_t gesture_id);

/**
 * @brief Send CMD_OLED_TEXT to STM32 (show 6-char message on OLED line 2).
 * @param text  Up to 6 ASCII characters (null-terminated, truncated if longer).
 */
void uart_stm32_send_oled_text(const char *text);

/**
 * @brief Send CMD_HAPTIC_BUZZ to STM32.
 * @param duration_ms  Buzz duration in ms (rounded to nearest 10 ms, max 2550 ms).
 */
void uart_stm32_send_haptic_buzz(uint32_t duration_ms);

/**
 * @brief Send CMD_HAPTIC_VIBRATE to STM32.
 * @param duration_ms  Vibration duration in ms (rounded to nearest 10 ms).
 */
void uart_stm32_send_haptic_vibrate(uint32_t duration_ms);

/**
 * @brief Send CMD_GESTURE_FEEDBACK to STM32.
 * Triggers the per-gesture unique haptic+OLED pattern on STM32.
 * @param gesture_id  Recognized gesture (gesture_id_t cast to uint8_t).
 */
void uart_stm32_send_gesture_feedback(uint8_t gesture_id);

/**
 * @brief Send CMD_OLED_CALIB to STM32 — shows live calibration display.
 *
 * @param state  0..3 = OPEN (Gata:3/2/1/TINE!), 4..7 = FIST (same steps)
 * @param adc    Current 12-bit ADC values [4]; scaled >>4 before sending.
 */
void uart_stm32_send_calib_adc(uint8_t state, const uint16_t adc[4]);

#endif /* UART_STM32_H */
