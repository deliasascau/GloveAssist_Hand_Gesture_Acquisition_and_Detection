// nvs_calib.c
// Implementation for NVS calibration storage for gesture thresholds

#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "nvs_calib.h"

#define NVS_PARTITION storage
#define NVS_CALIB_ID 0x100

static struct nvs_fs fs;
static bool nvs_ready = false;

static uint32_t crc32(const void *data, size_t len) {
    // Simple CRC32 (placeholder, use Zephyr's real implementation if available)
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return ~crc;
}

static void nvs_calib_init(void) {
    if (nvs_ready) return;
    fs.offset = FLASH_AREA_OFFSET(NVS_PARTITION);
    fs.sector_size = FLASH_AREA_SIZE(NVS_PARTITION);
    fs.sector_count = 1;
    nvs_mount(&fs);
    nvs_ready = true;
}

bool nvs_calib_save(const nvs_calib_data_t *data) {
    nvs_calib_init();
    nvs_calib_data_t tmp = *data;
    tmp.crc = crc32(tmp.fist_adc, sizeof(tmp.fist_adc));
    int rc = nvs_write(&fs, NVS_CALIB_ID, &tmp, sizeof(tmp));
    return rc == sizeof(tmp);
}

bool nvs_calib_load(nvs_calib_data_t *data) {
    nvs_calib_init();
    int rc = nvs_read(&fs, NVS_CALIB_ID, data, sizeof(*data));
    if (rc != sizeof(*data)) return false;
    uint32_t crc = crc32(data->fist_adc, sizeof(data->fist_adc));
    return (crc == data->crc);
}

bool nvs_calib_exists(void) {
    nvs_calib_data_t tmp;
    return nvs_calib_load(&tmp);
}
