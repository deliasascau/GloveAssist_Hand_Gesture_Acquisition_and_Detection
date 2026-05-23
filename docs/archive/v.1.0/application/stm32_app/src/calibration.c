/**
 * @file calibration.c
 * @brief Calibrare automata praguri flex sensors cu stocare Zephyr NVS
 *
 * Partiție NVS: "storage" (label din overlay) — 2KB la 0x0801F800
 * Chei NVS:
 *   ID 10 (CALIB_NVS_ID_PROFILE): profil versionat cu magic + checksum
 *   ID 1..3: format vechi, pastrat pentru migrare automata
 */

#include <zephyr/kernel.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "calibration.h"

LOG_MODULE_REGISTER(calibration, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- NVS filesystem handle ---------- */
static struct nvs_fs nvs;

/* ---------- Cache in RAM (evita NVS read la fiecare ciclu) ---------- */
static uint16_t thresh_cache[NUM_FLEX_SENSORS];
static uint16_t open_cache[NUM_FLEX_SENSORS];
static uint16_t bent_cache[NUM_FLEX_SENSORS];
static bool     nvs_ready       = false;
static bool     is_calibrated   = false;
static atomic_t start_requested;

/* ---------- Default praguri din app_config.h (fallback) ---------- */
static const uint16_t k_defaults[NUM_FLEX_SENSORS] = {
    GESTURE_THRESH_INDEX,   /* FINGER_INDEX  */
    GESTURE_THRESH_MIDDLE,  /* FINGER_MIDDLE */
    GESTURE_THRESH_RING,    /* FINGER_RING   */
    GESTURE_THRESH_PINKY,   /* FINGER_PINKY  */
};

#define CALIB_PROFILE_MAGIC   0x474C5643UL  /* "GLVC" */
#define CALIB_PROFILE_VERSION 1U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t  calibrated;
    uint8_t  reserved[3];
    uint16_t thresh[NUM_FLEX_SENSORS];
    uint16_t open_ref[NUM_FLEX_SENSORS];
    uint16_t bent_ref[NUM_FLEX_SENSORS];
    uint32_t checksum;
} calibration_profile_t;

static uint32_t checksum_fnv1a(const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t hash = 2166136261UL;

    for (size_t i = 0U; i < len; i++) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }

    return hash;
}

static uint32_t profile_checksum(const calibration_profile_t *profile)
{
    return checksum_fnv1a(profile,
                          sizeof(*profile) - sizeof(profile->checksum));
}

static bool threshold_valid(uint16_t value)
{
    return ((value >= (uint16_t)ADC_MIN_VALID) &&
            (value <= (uint16_t)ADC_MAX_VALID));
}

static bool thresholds_valid(const uint16_t thresh[NUM_FLEX_SENSORS])
{
    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        if (!threshold_valid(thresh[i])) {
            return false;
        }
    }

    return true;
}

static void profile_prepare(calibration_profile_t *profile,
                            const uint16_t thresh[NUM_FLEX_SENSORS],
                            const uint16_t open_ref[NUM_FLEX_SENSORS],
                            const uint16_t bent_ref[NUM_FLEX_SENSORS],
                            bool calibrated)
{
    (void)memset(profile, 0, sizeof(*profile));
    profile->magic = CALIB_PROFILE_MAGIC;
    profile->version = CALIB_PROFILE_VERSION;
    profile->size = (uint16_t)sizeof(*profile);
    profile->calibrated = calibrated ? 1U : 0U;
    (void)memcpy(profile->thresh, thresh, sizeof(profile->thresh));
    (void)memcpy(profile->open_ref, open_ref, sizeof(profile->open_ref));
    (void)memcpy(profile->bent_ref, bent_ref, sizeof(profile->bent_ref));
    profile->checksum = profile_checksum(profile);
}

static bool profile_validate(const calibration_profile_t *profile)
{
    bool valid = true;

    if (profile->magic != CALIB_PROFILE_MAGIC) {
        valid = false;
    } else if (profile->version != CALIB_PROFILE_VERSION) {
        valid = false;
    } else if (profile->size != (uint16_t)sizeof(*profile)) {
        valid = false;
    } else if (profile->checksum != profile_checksum(profile)) {
        valid = false;
    }

    if (valid) {
        for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
            if (!threshold_valid(profile->thresh[i])) {
                valid = false;
                break;
            }
        }
    }

    return valid;
}

static void load_profile_to_cache(const calibration_profile_t *profile)
{
    (void)memcpy(thresh_cache, profile->thresh, sizeof(thresh_cache));
    (void)memcpy(open_cache, profile->open_ref, sizeof(open_cache));
    (void)memcpy(bent_cache, profile->bent_ref, sizeof(bent_cache));
    is_calibrated = (profile->calibrated != 0U);
}

static void fill_default_cache(void)
{
    (void)memcpy(thresh_cache, k_defaults, sizeof(thresh_cache));
    (void)memset(open_cache, 0, sizeof(open_cache));
    (void)memset(bent_cache, 0, sizeof(bent_cache));
    is_calibrated = false;
}

static void warn_threshold_drift(void)
{
    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        uint32_t def  = (uint32_t)k_defaults[i];
        uint32_t cur  = (uint32_t)thresh_cache[i];
        uint32_t diff = (cur > def) ? (cur - def) : (def - cur);
        uint32_t pct  = (def > 0U) ? ((diff * 100U) / def) : 0U;

        if (pct > (uint32_t)CALIB_DRIFT_PERCENT) {
            LOG_WRN("Finger %u: prag=%u, default=%u, deriva=%u%% - "
                    "recalibrare recomandata",
                    i, thresh_cache[i], k_defaults[i], pct);
        }
    }
}

static int save_profile(bool calibrated)
{
    calibration_profile_t profile;

    if (!nvs_ready) {
        LOG_ERR("NVS nu e gata - salvare imposibila");
        return -ENODEV;
    }

    if (!thresholds_valid(thresh_cache)) {
        LOG_ERR("Prag invalid - profilul nu este salvat");
        return -EINVAL;
    }

    profile_prepare(&profile, thresh_cache, open_cache, bent_cache, calibrated);

    ssize_t rc = nvs_write(&nvs, CALIB_NVS_ID_PROFILE,
                           &profile, sizeof(profile));
    if (rc < 0) {
        LOG_ERR("nvs_write profile failed: %d", (int)rc);
        return (int)rc;
    }

    is_calibrated = calibrated;
    LOG_INF("Profil calibrare salvat v%u checksum=0x%08X",
            CALIB_PROFILE_VERSION, profile.checksum);
    return 0;
}

static bool try_load_profile(void)
{
    calibration_profile_t profile;
    ssize_t bytes = nvs_read(&nvs, CALIB_NVS_ID_PROFILE,
                             &profile, sizeof(profile));

    if (bytes != (ssize_t)sizeof(profile)) {
        return false;
    }

    if (!profile_validate(&profile)) {
        LOG_WRN("Profil calibrare NVS invalid - fallback/migrare");
        return false;
    }

    load_profile_to_cache(&profile);
    LOG_INF("Profil calibrare incarcat v%u: I=%u M=%u R=%u P=%u",
            profile.version,
            thresh_cache[FINGER_INDEX], thresh_cache[FINGER_MIDDLE],
            thresh_cache[FINGER_RING],  thresh_cache[FINGER_PINKY]);
    warn_threshold_drift();
    return true;
}

static bool try_migrate_legacy(void)
{
    uint16_t legacy_thresh[NUM_FLEX_SENSORS];
    ssize_t bytes = nvs_read(&nvs, CALIB_NVS_ID_THRESH,
                             legacy_thresh, sizeof(legacy_thresh));

    if (bytes != (ssize_t)sizeof(legacy_thresh)) {
        return false;
    }

    if (!thresholds_valid(legacy_thresh)) {
        LOG_WRN("Format vechi gasit, dar pragurile sunt invalide");
        return false;
    }

    (void)memcpy(thresh_cache, legacy_thresh, sizeof(thresh_cache));

    bytes = nvs_read(&nvs, CALIB_NVS_ID_OPEN, open_cache, sizeof(open_cache));
    if (bytes != (ssize_t)sizeof(open_cache)) {
        (void)memset(open_cache, 0, sizeof(open_cache));
    }

    bytes = nvs_read(&nvs, CALIB_NVS_ID_BENT, bent_cache, sizeof(bent_cache));
    if (bytes != (ssize_t)sizeof(bent_cache)) {
        (void)memset(bent_cache, 0, sizeof(bent_cache));
    }

    is_calibrated = true;
    LOG_INF("Migrare calibrare legacy -> profil versionat");
    (void)save_profile(true);
    warn_threshold_drift();
    return true;
}

/* ===================================================================== */
/*                         INIT                                          */
/* ===================================================================== */

int calibration_init(void)
{
    int rc;

    /* Obtine adresa si dimensiunea partitiei "storage" din flash map */
    const struct flash_area *fa;
    rc = flash_area_open(PARTITION_ID(storage_partition), &fa);
    if (rc < 0) {
        LOG_ERR("flash_area_open failed: %d", rc);
        /* Fallback la defaults in RAM chiar daca NVS nu e disponibil */
        fill_default_cache();
        return rc;
    }

    nvs.flash_device = fa->fa_dev;
    nvs.offset       = fa->fa_off;
    nvs.sector_size  = 1024U; /* 1KB per pagina STM32F103 */
    nvs.sector_count = 2U;    /* 2 pagini = 2KB total */

    rc = nvs_mount(&nvs);
    if (rc < 0) {
        LOG_ERR("nvs_mount failed: %d — folosesc defaults", rc);
        flash_area_close(fa);
        fill_default_cache();
        return rc;
    }
    nvs_ready = true;
    flash_area_close(fa);

    if (!try_load_profile() && !try_migrate_legacy()) {
        LOG_INF("NVS gol/corupt - scriu profil defaults");
        fill_default_cache();
        rc = save_profile(false);
        if (rc < 0) {
            LOG_ERR("Scriere profil defaults esuata: %d", rc);
        }
    }

    return 0;
}

/* ===================================================================== */
/*                         GET / QUERY                                   */
/* ===================================================================== */

uint16_t calibration_get_thresh(uint8_t finger)
{
    if (finger >= (uint8_t)NUM_FLEX_SENSORS) {
        return (uint16_t)GESTURE_THRESHOLD_BENT;
    }
    return thresh_cache[finger];
}

bool calibration_is_calibrated(void)
{
    return is_calibrated;
}

/* ===================================================================== */
/*                         SAVE                                          */
/* ===================================================================== */

int calibration_save(const uint16_t thresh[NUM_FLEX_SENSORS])
{
    if (!thresholds_valid(thresh)) {
        return -EINVAL;
    }

    (void)memcpy(thresh_cache, thresh, sizeof(thresh_cache));
    int rc = save_profile(true);
    if (rc == 0) {
        LOG_INF("Praguri salvate: I=%u M=%u R=%u P=%u",
                thresh_cache[FINGER_INDEX],  thresh_cache[FINGER_MIDDLE],
                thresh_cache[FINGER_RING],   thresh_cache[FINGER_PINKY]);
    }
    return rc;
}

int calibration_save_one(uint8_t finger, uint16_t value)
{
    if (finger >= (uint8_t)NUM_FLEX_SENSORS) {
        return -EINVAL;
    }
    if (!threshold_valid(value)) {
        return -EINVAL;
    }
    thresh_cache[finger] = value;
    return save_profile(true);
}

int calibration_reset_to_defaults(void)
{
    LOG_INF("Reset calibrare la defaults din app_config.h");
    fill_default_cache();
    return save_profile(false);
}

void calibration_request_start(void)
{
    atomic_set(&start_requested, 1);
}

bool calibration_take_start_request(void)
{
    return (atomic_set(&start_requested, 0) != 0);
}

/* ===================================================================== */
/*                         SESIUNE AUTO                                  */
/* ===================================================================== */

/**
 * @brief Masoara media ADC pe CALIB_MEASURE_MS pentru toate canalele.
 *
 * @param adc_channels  Array de ADC specs
 * @param out           Output: medie per canal (array NUM_FLEX_SENSORS)
 * @return 0 la succes, negativ la eroare ADC
 */
static int measure_average(const struct adc_dt_spec *adc_channels,
                            uint16_t out[NUM_FLEX_SENSORS])
{
    uint32_t sum[NUM_FLEX_SENSORS] = {0U};
    uint32_t count = 0U;
    int16_t  buf;
    struct adc_sequence seq = {
        .buffer      = &buf,
        .buffer_size = sizeof(buf),
    };

    int64_t deadline = k_uptime_get() + (int64_t)CALIB_MEASURE_MS;

    while (k_uptime_get() < deadline) {
        for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
            (void)adc_sequence_init_dt(&adc_channels[ch], &seq);
            int rc = adc_read_dt(&adc_channels[ch], &seq);
            if (rc < 0) {
                LOG_WRN("ADC ch%u eroare: %d", ch, rc);
                continue;
            }
            sum[ch] += (uint32_t)buf;
        }
        count++;
        k_msleep((uint32_t)CALIB_MEASURE_MS / CALIB_NUM_SAMPLES);
    }

    if (count == 0U) {
        return -EIO;
    }

    for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
        out[ch] = (uint16_t)(sum[ch] / count);
    }
    return 0;
}

int calibration_start(const struct adc_dt_spec *adc_channels)
{
    uint16_t open_val[NUM_FLEX_SENSORS] = {0U};
    uint16_t bent_val[NUM_FLEX_SENSORS] = {0U};
    uint16_t new_thresh[NUM_FLEX_SENSORS];
    int rc;

    LOG_INF("=== CALIBRARE START ===");
    LOG_INF("Faza 1: tine toate degetele DREPTE — %u secunde",
            CALIB_MEASURE_MS / 1000U);

    rc = measure_average(adc_channels, open_val);
    if (rc < 0) {
        LOG_ERR("Masurare OPEN esuata: %d", rc);
        return rc;
    }
    LOG_INF("OPEN: I=%u M=%u R=%u P=%u",
            open_val[FINGER_INDEX], open_val[FINGER_MIDDLE],
            open_val[FINGER_RING],  open_val[FINGER_PINKY]);

    LOG_INF("Faza 2: tine toate degetele INDOITE — %u secunde",
            CALIB_MEASURE_MS / 1000U);

    rc = measure_average(adc_channels, bent_val);
    if (rc < 0) {
        LOG_ERR("Masurare BENT esuata: %d", rc);
        return rc;
    }
    LOG_INF("BENT: I=%u M=%u R=%u P=%u",
            bent_val[FINGER_INDEX], bent_val[FINGER_MIDDLE],
            bent_val[FINGER_RING],  bent_val[FINGER_PINKY]);

    /* Calculeaza pragul: (open + bent) / 2 per canal */
    for (uint8_t i = 0U; i < (uint8_t)NUM_FLEX_SENSORS; i++) {
        new_thresh[i] = (uint16_t)(((uint32_t)open_val[i] +
                                    (uint32_t)bent_val[i]) / 2U);
    }

    LOG_INF("PRAG CALCULAT: I=%u M=%u R=%u P=%u",
            new_thresh[FINGER_INDEX], new_thresh[FINGER_MIDDLE],
            new_thresh[FINGER_RING],  new_thresh[FINGER_PINKY]);

    (void)memcpy(thresh_cache, new_thresh, sizeof(thresh_cache));
    (void)memcpy(open_cache, open_val, sizeof(open_cache));
    (void)memcpy(bent_cache, bent_val, sizeof(bent_cache));

    rc = save_profile(true);
    if (rc == 0) {
        LOG_INF("=== CALIBRARE COMPLETA — praguri salvate in NVS ===");
    }
    return rc;
}
