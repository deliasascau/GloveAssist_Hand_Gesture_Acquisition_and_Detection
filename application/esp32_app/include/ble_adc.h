#ifndef BLE_ADC_H
#define BLE_ADC_H

#include <stdint.h>
#include "frame_protocol.h"
#include <stdbool.h>

#if defined(CONFIG_BT)

int  ble_adc_init(void);
int  ble_adc_send_values(const uint16_t adc[4]);
int  ble_adc_send_gesture(gesture_id_t gesture, const uint16_t adc[4]);
void ble_adc_process(void);

/** Send a raw text notification over BLE NUS TX (max 20 bytes). */
int  ble_adc_send_text(const char *text);

/* Called by main after a stable gesture is detected. */
void ble_adc_set_last_gesture(gesture_id_t g);

/**
 * @brief Returns true once if BLE caregiver sent calibrate command ('C'/'c').
 * Clears the flag on read.
 */
bool ble_adc_pop_calibration_request(void);

/**
 * @brief Checks if caregiver sent OK/ACK via BLE. Clears the flag on read.
 * @param out_gesture  Filled with the gesture that was active when OK was
 *                     pressed (may be GESTURE_NONE if no gest was active).
 * @return true if an ACK was pending, false otherwise.
 * Main loop must handle: send CMD_CAREGIVER_ACK to STM32 and BLE
 * "Understood" notification back to phone.
 */
bool ble_adc_pop_caregiver_ack_request(gesture_id_t *out_gesture);

#else /* CONFIG_BT not set — provide no-op stubs */

#include "gesture.h"

static inline int  ble_adc_init(void)                                            { return 0; }
static inline int  ble_adc_send_values(const uint16_t *a)                        { (void)a; return 0; }
static inline int  ble_adc_send_gesture(gesture_id_t g, const uint16_t *a)       { (void)g; (void)a; return 0; }
static inline void ble_adc_process(void)                                         {}
static inline int  ble_adc_send_text(const char *t)                              { (void)t; return 0; }
static inline void ble_adc_set_last_gesture(gesture_id_t g)                      { (void)g; }
static inline bool ble_adc_pop_calibration_request(void)                         { return false; }
static inline bool ble_adc_pop_caregiver_ack_request(gesture_id_t *out)     { if(out){*out=GESTURE_NONE;} return false; }

#endif /* CONFIG_BT */

#endif /* BLE_ADC_H */
