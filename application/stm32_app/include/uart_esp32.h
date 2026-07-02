/*
 * uart_esp32.h — STM32 UART communication with ESP32.
 *
 * USART1: PA9 TX → ESP32 GPIO16 RX
 *         PA10 RX ← ESP32 GPIO17 TX
 * Baud: 57600, 8N1
 *
 * Design:
 *   TX: interrupt-driven FIFO fill (non-blocking send).
 *   RX: ISR queues raw bytes; a UART RX thread parses/authenticates frames.
 */

#ifndef UART_ESP32_H_
#define UART_ESP32_H_

#include <stdbool.h>
#include "frame_protocol.h"

/** @brief Initialise USART1 interrupt-driven TX and threaded RX. */
void uart_esp32_init(void);

/** @brief Send one HMAC-authenticated frame over USART1 (drops silently if TX busy). */
void uart_esp32_send_hmac_frame(const frame_hmac_t *frame);

/** @brief Send HMAC-only SESSION_ACK to ESP32. */
void uart_esp32_send_session_ack(uint32_t session_nonce);

/**
 * @brief Poll one pending SESSION_HELLO nonce from ESP32.
 */
bool uart_esp32_poll_session_hello(uint32_t *session_nonce);

/**
 * @brief Non-blocking poll: returns true if a STATUS_ACK or HEARTBEAT frame
 *        has been received since the last call. Clears the flag on return.
 */
bool uart_esp32_poll_ack(void);

/**
 * @brief Non-blocking poll: returns true if a COMMAND frame is pending.
 *        Copies the frame into @p out and clears the pending flag.
 *        Use irq_lock internally — safe to call from the main loop.
 */
bool uart_esp32_poll_cmd(frame_t *out);

/**
 * @brief Returns diagnostic counters. All counters reset to 0 after read.
 *   cmds          : good COMMAND frames received
 *   bad_frames    : all frames failing authentication/SOF checks
 *   bad_cmd_frames: bad frames whose type byte == FRAME_TYPE_COMMAND
 */
void uart_esp32_get_rx_diag(uint8_t *cmds, uint8_t *bad_frames,
                             uint8_t *bad_cmd_frames);

/**
 * @brief Returns true once if a CMD-type frame failed authentication since
 *        the last call.  Instant feedback — does not wait 10 s for the diag.
 */
bool uart_esp32_poll_bad_cmd(void);

#endif /* UART_ESP32_H_ */
