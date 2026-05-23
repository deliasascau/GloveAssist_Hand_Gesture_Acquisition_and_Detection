// nvs_calib.h
// Header for NVS calibration storage for gesture thresholds

#ifndef NVS_CALIB_H
#define NVS_CALIB_H

#include <stdint.h>
#include <stdbool.h>

#define NUM_FLEX_SENSORS 4

// Struct for storing calibration values (e.g., FIST reference)
typedef struct {
    uint16_t fist_adc[NUM_FLEX_SENSORS];
    uint32_t crc;
} nvs_calib_data_t;

// Save calibration data to NVS
bool nvs_calib_save(const nvs_calib_data_t *data);

// Load calibration data from NVS (returns true if valid)
bool nvs_calib_load(nvs_calib_data_t *data);

// Check if calibration is present in NVS
bool nvs_calib_exists(void);

#endif // NVS_CALIB_H
