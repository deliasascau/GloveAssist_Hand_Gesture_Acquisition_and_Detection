/**
 * @file uart_comm.h
 * @brief UART inter-MCU communication thread API (STM32 side)
 *
 * STM32 sends frames to ESP32 via USART1 (PA9 TX, PA10 RX).
 * Protocol: glove_frame_t, 12 bytes — SOF + TYPE + SEQ + PAYLOAD[8] XOR + CRC8.
 */

#ifndef UART_COMM_STM32_H
#define UART_COMM_STM32_H

#include "frame_protocol.h"

/**
 * @brief UART comm thread entry point (auto-started via K_THREAD_DEFINE).
 *
 * Periodically sends sensor/gesture frames to ESP32 and receives
 * heartbeat/command frames back. Calls safety_heartbeat_received() on HB RX.
 */
void uart_comm_thread_entry(void *p1, void *p2, void *p3);

#endif /* UART_COMM_STM32_H */
