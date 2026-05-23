/**
 * @file haptic_ui.h
 * @brief Haptic feedback (motor + buzzer) and OLED display API (STM32)
 */

#ifndef HAPTIC_UI_H
#define HAPTIC_UI_H

#include "common_types.h"

/**
 * @brief Haptic/UI thread entry point.
 *
 * Waits for K_EVENT flags and plays motor/buzzer patterns (software PWM):
 *   EVT_GESTURE_ACK  — short pulse on successful gesture detection
 *   EVT_ERROR        — double-beep error signal
 *   EVT_SOS_ALARM    — continuous SOS pattern (degraded mode)
 */
void haptic_thread_entry(void *p1, void *p2, void *p3);

/**
 * @brief Display a gesture and play the normal acknowledgement pattern.
 */
void haptic_notify_gesture(u8_t gesture_id);

/**
 * @brief Signal that a remote message/notification was received.
 */
void haptic_notify_message(void);

/**
 * @brief Signal the haptic thread to play an error pattern.
 *
 * Safe to call from any thread or ISR context.
 * No-op if CONFIG_PWM is not enabled.
 */
void haptic_notify_error(void);

/**
 * @brief Signal the haptic thread to play the degraded-mode SOS pattern.
 */
void haptic_notify_sos(void);

/**
 * @brief Signal that phone confirmed message ("inteles") — motor 2×pulse + OLED OK.
 *
 * Safe to call from any thread context.
 */
void haptic_notify_ack_received(void);

#endif /* HAPTIC_UI_H */
