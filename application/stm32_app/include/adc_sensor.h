/*
 * adc_sensor.h — ADC1 + DMA1 acquisition (4 flex sensors PA0..PA3).
 *
 * Uses direct register writes — no Zephyr ADC driver needed.
 * The ADC node in the overlay is intentionally disabled to avoid conflicts.
 */

#ifndef ADC_SENSOR_H_
#define ADC_SENSOR_H_

#include <stdint.h>

#define ADC_NUM_CHANNELS 4U

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

#endif /* ADC_SENSOR_H_ */
