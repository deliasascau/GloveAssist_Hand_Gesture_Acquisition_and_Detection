/**
 * @file sensor_filter.c
 * @brief Moving average filter for flex sensor data (ESP32 brain)
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "sensor_filter.h"

LOG_MODULE_REGISTER(sensor_filter, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Filter state (moved from STM32) ---------- */
static uint16_t filter_buf[NUM_FLEX_SENSORS][FILTER_WINDOW_SIZE];
static uint8_t  filter_idx[NUM_FLEX_SENSORS];
static uint32_t filter_sum[NUM_FLEX_SENSORS];
static bool     initialized = false;

int sensor_filter_init(void)
{
    if (initialized) {
        LOG_WRN("sensor_filter already initialized");
        return 0;
    }

    (void)memset(filter_buf, 0, sizeof(filter_buf));
    (void)memset(filter_idx, 0, sizeof(filter_idx));
    (void)memset(filter_sum, 0, sizeof(filter_sum));

    initialized = true;
    LOG_INF("Sensor filter initialized (window size: %u)", FILTER_WINDOW_SIZE);
    return 0;
}

uint16_t sensor_filter_update(uint8_t channel, uint16_t raw_value)
{
    if (!initialized) {
        LOG_ERR("Filter not initialized");
        return raw_value;
    }

    if (channel >= (uint8_t)NUM_FLEX_SENSORS) {
        LOG_ERR("Invalid channel: %u", channel);
        return raw_value;
    }

    /* Clamp to valid ADC range */
    if (raw_value > (uint16_t)ADC_MAX_VALID) {
        raw_value = (uint16_t)ADC_MAX_VALID;
    }

    /* Update moving average */
    uint8_t idx = filter_idx[channel];
    filter_sum[channel] -= filter_buf[channel][idx];
    filter_buf[channel][idx] = raw_value;
    filter_sum[channel] += raw_value;
    filter_idx[channel] = (uint8_t)((idx + 1U) % FILTER_WINDOW_SIZE);

    return (uint16_t)(filter_sum[channel] / FILTER_WINDOW_SIZE);
}

uint16_t sensor_filter_get(uint8_t channel)
{
    if (!initialized || channel >= (uint8_t)NUM_FLEX_SENSORS) {
        return 0U;
    }

    return (uint16_t)(filter_sum[channel] / FILTER_WINDOW_SIZE);
}

void sensor_filter_get_all(uint16_t out[NUM_FLEX_SENSORS])
{
    if (!initialized || out == NULL) {
        return;
    }

    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        out[i] = (uint16_t)(filter_sum[i] / FILTER_WINDOW_SIZE);
    }
}

void sensor_filter_reset(void)
{
    if (!initialized) {
        return;
    }

    LOG_INF("Resetting filter state");
    (void)memset(filter_buf, 0, sizeof(filter_buf));
    (void)memset(filter_idx, 0, sizeof(filter_idx));
    (void)memset(filter_sum, 0, sizeof(filter_sum));
}

uint8_t sensor_filter_get_window_size(void)
{
    return (uint8_t)FILTER_WINDOW_SIZE;
}
