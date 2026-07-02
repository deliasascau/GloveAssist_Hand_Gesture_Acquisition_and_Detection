/*
 * adc_sensor.h — ADC1 + DMA1 acquisition (4 flex sensors PA0..PA3).
 *
 * Uses direct register writes — no Zephyr ADC driver needed.
 * The ADC node in the overlay is intentionally disabled to avoid conflicts.
 */

#ifndef ADC_SENSOR_H_
#define ADC_SENSOR_H_

#include <stdint.h>
#include <stdbool.h>

#define ADC_NUM_CHANNELS 4U

/*
 * Sensor fault detection thresholds.
 *
 * Detect only rail-level wiring faults, not valid bent-finger gestures.
 * Calibration shows normal bent values can be around 40..200 ADC counts, so
 * the LOW threshold must stay well below that range.  Values near 0 are treated
 * as short-to-GND candidates; values near full-scale are treated as short-to-3V3
 * candidates.  Counts are consecutive samples from the STM32 main loop.
 */
#define ADC_SENSOR_FAULT_LOW_THRESHOLD    20U
#define ADC_SENSOR_FAULT_HIGH_THRESHOLD 4080U
#define ADC_SENSOR_FAULT_ASSERT_COUNT    100U
#define ADC_SENSOR_FAULT_RECOVER_COUNT   200U

typedef struct {
    uint8_t active_mask; /* bit i = channel i is currently faulted */
    uint8_t low_mask;    /* bit i = channel i faulted low/near GND  */
    uint8_t high_mask;   /* bit i = channel i faulted high/near VCC */
} adc_sensor_fault_status_t;

/**
 * @brief Initialise ADC1 with DMA1 circular mode for 4 channels (PA0..PA3).
 *        Must be called once from main() before the main loop.
 */
void adc_sensor_init(void);

/**
 * @brief Copy the latest DMA-captured ADC values into @p out[ADC_NUM_CHANNELS].
 *        Safe to call from the main loop while DMA runs continuously.
 */
void adc_sensor_read(uint16_t out[ADC_NUM_CHANNELS]);

/**
 * @brief Reset per-channel fault debounce state.
 */
void adc_sensor_fault_reset(void);

/**
 * @brief Update per-channel fault debounce/recovery state from latest samples.
 *
 * @param sample Latest ADC values [index, middle, ring, pinky].
 * @param status Optional output copy of current fault state.
 * @return true if active fault masks changed since the previous call.
 */
bool adc_sensor_fault_update(const uint16_t sample[ADC_NUM_CHANNELS],
                             adc_sensor_fault_status_t *status);

/**
 * @brief Copy current fault state into @p status.
 */
void adc_sensor_fault_get(adc_sensor_fault_status_t *status);

#endif /* ADC_SENSOR_H_ */
