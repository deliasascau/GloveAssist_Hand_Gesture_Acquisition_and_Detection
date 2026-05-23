#ifndef BLE_ADC_H
#define BLE_ADC_H

#include <stdint.h>
#include "frame_protocol.h"
#include <stdbool.h>

int  ble_adc_init(void);
int  ble_adc_send_values(const uint16_t adc[4]);
int  ble_adc_send_gesture(gesture_id_t gesture, const uint16_t adc[4]);

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
 * @brief Returns true once if BLE caregiver sent 'U' (OTA / reboot request).
 * Clears the flag on read.
 */
bool ble_adc_pop_ota_request(void);

/**
 * @brief Returns confirmed gesture_id if caregiver sent OK/ACK, else GESTURE_NONE.
 * Clears the flag on read. Main loop must handle: send CMD_CAREGIVER_ACK to STM32
 * and BLE "Understood" notification.
 */
gesture_id_t ble_adc_pop_caregiver_ack_request(void);

#endif /* BLE_ADC_H */
