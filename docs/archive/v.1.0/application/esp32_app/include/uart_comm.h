/**
 * @file uart_comm.h
 * @brief UART inter-MCU communication thread API (ESP32 side)
 *
 * ESP32 receives frames from STM32 via UART2 (GPIO16 RX, GPIO17 TX).
 * Protocol: glove_frame_t, 12 bytes — SOF + TYPE + SEQ + PAYLOAD[8] XOR + CRC8.
 */

#ifndef UART_COMM_ESP32_H
#define UART_COMM_ESP32_H

#include <stdint.h>
#include "frame_protocol.h"

/**
 * @brief UART RX thread entry point (auto-started via K_THREAD_DEFINE).
 *
 * Receives complete frames from STM32, validates CRC, dispatches payloads
 * to the BLE TX queue. Sends heartbeat responses asynchronously.
 */
void uart_comm_thread_entry(void *p1, void *p2, void *p3);

/**
 * @brief Send a frame to STM32 via UART (called from any thread/timer).
 *
 * @param frame  Pointer to the frame to transmit.
 * @return 0 on success, negative errno on failure.
 */
int uart_comm_send(const glove_frame_t *frame);

/**
 * @brief Impacheteaza o comanda BLE intr-un frame UART si o trimite la STM32.
 *
 * Apelat din comms_ble.c::on_rx_write() cand telefonul trimite o comanda.
 * Construieste frame MSG_TYPE_COMMAND, aplica XOR+CRC, transmite pe UART.
 *
 * @param cmd  Pointer la structura comanda deja populata.
 * @return 0 la succes, negativ la eroare.
 */
int uart_comm_send_command(const frame_command_payload_t *cmd);

/**
 * @brief Send a compact sniff/security diagnostics report over BLE.
 *
 * This report summarizes UART traffic health (CRC drops, replay drops,
 * honeypot count, lockdown state) and is used for anti-sniffing validation.
 *
 * @return 0 on success, negative errno on failure.
 */
int uart_comm_send_sniff_report(void);

#endif /* UART_COMM_ESP32_H */
