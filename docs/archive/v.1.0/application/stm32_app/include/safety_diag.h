/**
 * @file safety_diag.h
 * @brief Safety subsystem API — watchdog, sensor validation, heartbeat monitor
 *
 * IEC 61508 Functional Safety:
 *   - Sensor open/short-circuit detection with fallback
 *   - IWDG watchdog (2s timeout)
 *   - ESP32 heartbeat monitoring → degraded mode with local alarm
 */

#ifndef SAFETY_DIAG_H
#define SAFETY_DIAG_H

#include "common_types.h"
#include "error_codes.h"

/**
 * @brief Initialise watchdog and heartbeat monitor.
 * @return 0 on success, negative on error.
 */
int safety_init(void);

/**
 * @brief Feed the hardware watchdog. Call from main processing loop.
 */
void safety_watchdog_feed(void);

/**
 * @brief Validate a sensor reading (open/short-circuit detection).
 *
 * After FAULT_THRESHOLD consecutive faults on a channel, the output
 * falls back to the last known good reading.
 *
 * @param ch   Channel index (0..3).
 * @param raw  Raw ADC reading.
 * @param out  Output: validated or fallback value.
 * @return ERR_SENSOR_OK or specific error code.
 */
error_code_t safety_validate_sensor(uint8_t ch, uint16_t raw, uint16_t *out);

/**
 * @brief Check if all sensors are in a fault state.
 * @return true if all channels have exceeded the fault threshold.
 */
bool safety_all_sensors_faulted(void);

/**
 * @brief Enter safe state: disable motor, stop BLE, show FAULT on OLED.
 */
void safety_enter_lockdown(void);

/**
 * @brief Notify safety subsystem that a heartbeat was received from ESP32.
 *
 * Called by UART RX thread when MSG_TYPE_HEARTBEAT is received.
 */
void safety_heartbeat_received(void);

/**
 * @brief Check if ESP32 is considered alive (heartbeat within timeout).
 * @return true if ESP32 heartbeat is current.
 */
bool safety_esp32_alive(void);

/**
 * @brief Check if system is in degraded mode (ESP32 unreachable).
 * @return true if operating in degraded mode.
 */
bool safety_is_degraded(void);

/**
 * @brief Safety monitor thread entry point (auto-started).
 *
 * Periodically checks ESP32 heartbeat, feeds watchdog, and triggers
 * local alarm if ESP32 is unresponsive for HEARTBEAT_TIMEOUT_MS.
 */
void safety_thread_entry(void *p1, void *p2, void *p3);

#endif /* SAFETY_DIAG_H */
