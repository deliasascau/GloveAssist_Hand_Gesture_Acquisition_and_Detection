#ifndef ALERT_H
#define ALERT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * GPIO emergency backup channel — STM32 side.
 *
 * Hardware:
 *   PB0 (ALERT_OUT) -> wire -> ESP32 GPIO18 (ALERT_IN)
 *   PB1 (ACK_IN)    <- wire <- ESP32 GPIO19 (ACK_OUT)
 *
 * Protocol:
 *   STM32 sets ALERT high when UART is lost + critical gesture detected.
 *   STM32 holds ALERT high until ACK pin goes high or timeout (10 s).
 *   ESP32 reads ALERT, sends BLE emergency, then raises ACK for 200 ms.
 */

/** Initialise GPIO pins — call once during boot. */
void alert_init(void);

/** Assert ALERT line (PB0 high). */
void alert_raise(void);

/** De-assert ALERT line (PB0 low). */
void alert_clear(void);

/** Return true if ACK line (PB1) is currently high. */
bool alert_ack_received(void);

/**
 * Minimal local gesture classifier for critical gestures.
 * Only used when UART link is lost (no ESP32 side classification).
 *
 * Returns true if adc[] represents the HELP gesture
 * (all four fingers bent above FLEX_THRESH = 2048).
 * Mirrors GESTURE_HELP classification on the ESP32 side.
 * Threshold: 2048 (mid-scale 12-bit ADC).
 */
bool alert_is_help_gesture(const uint16_t adc[4]);

#endif /* ALERT_H */
