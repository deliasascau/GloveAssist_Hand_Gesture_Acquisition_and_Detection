#ifndef GPIO_ALERT_H
#define GPIO_ALERT_H

#include <stdbool.h>

/**
 * GPIO emergency backup channel — ESP32 side.
 *
 * Hardware:
 *   GPIO18 (ALERT_IN)  <- wire <- STM32 PB0 (ALERT_OUT)
 *   GPIO19 (ACK_OUT)   -> wire -> STM32 PB1 (ACK_IN)
 *
 * Protocol:
 *   ESP32 watches GPIO18 for rising edge (interrupt driven).
 *   When ALERT detected: sends BLE emergency notification,
 *   raises GPIO19 (ACK) for 200 ms, then clears it.
 */

/** Initialise GPIO pins + interrupt. Call once during boot. */
int gpio_alert_init(void);

/**
 * Poll function — call from main loop.
 * Returns true if an unhandled ALERT was detected since last call.
 * Automatically asserts ACK for 200 ms and clears the flag.
 */
bool gpio_alert_poll(void);

#endif /* GPIO_ALERT_H */
