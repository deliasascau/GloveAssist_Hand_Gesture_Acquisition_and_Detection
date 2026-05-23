/*
 * calibration.c - Per-gesture flex sensor calibration with NVS persistence.
 *
 * State machine: IDLE → NONE → WATER → WC → FOOD → HELP → IDLE
 *
 * Each phase:
 *   - CALIB_SETTLE_MS settle (user preps the gesture, countdown 3/2/1)
 *   - CALIB_PHASE_MS  sampling (ADC averages accumulated)
 *   - On timeout: store average in s_profiles[phase][*], advance to next phase
 *
 * Profiles saved as "calib/prof5" in NVS (5 × 4 × uint16 = 40 bytes).
 * Schema version "calib/ver" = CALIB_SCHEMA_VER ensures stale data is discarded.
 */

#include "calibration.h"

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(calibration, LOG_LEVEL_INF);

#define SETTINGS_KEY_PROF5  "calib/prof5"
#define SETTINGS_KEY_VER    "calib/ver"

static const char *const s_phase_names[CALIB_NUM_GESTURES] = {
    "NONE", "WATER", "WC", "FOOD", "HELP"
};

/* ── State machine ──────────────────────────────────────────────────────── */

static int8_t   s_phase       = -1;  /* -1=IDLE, 0-4=current gesture phase */
static uint32_t s_phase_start;
static bool     s_collecting;

/* Per-phase running accumulators (reset at each phase transition). */
static uint32_t s_sum[CALIB_NUM_FINGERS];
static uint32_t s_count;

/* Computed profiles: profiles[gesture_idx][finger_idx] */
static uint16_t s_profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS];

/* ── NVS load buffers ───────────────────────────────────────────────────── */

static uint16_t s_nvs_prof[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS];
static bool     s_nvs_prof_valid;
static uint8_t  s_nvs_schema_ver;
static bool     s_nvs_ver_valid;

static int calib_settings_set(const char *key, size_t len,
                               settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(key, "prof5") == 0) {
        if (len != sizeof(s_nvs_prof)) {
            LOG_WRN("calib/prof5 NVS: unexpected size %u (expected %u)",
                    (unsigned)len, (unsigned)sizeof(s_nvs_prof));
            return -EINVAL;
        }
        ssize_t n = read_cb(cb_arg, s_nvs_prof, sizeof(s_nvs_prof));
        if (n == (ssize_t)sizeof(s_nvs_prof)) {
            s_nvs_prof_valid = true;
        }
    } else if (strcmp(key, "ver") == 0) {
        if (len == sizeof(s_nvs_schema_ver)) {
            ssize_t n = read_cb(cb_arg, &s_nvs_schema_ver, sizeof(s_nvs_schema_ver));
            if (n == (ssize_t)sizeof(s_nvs_schema_ver)) {
                s_nvs_ver_valid = true;
            }
        }
    }
    /* Old keys ("thresh", "prof") are ignored — no match, no error. */
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(calib, "calib", NULL, calib_settings_set,
                                NULL, NULL);

/* ── Public API ─────────────────────────────────────────────────────────── */

int calibration_init(uint16_t profiles_out[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS])
{
    int err = settings_subsys_init();
    if (err != 0) {
        LOG_ERR("settings_subsys_init failed: %d", err);
        return err;
    }

    s_nvs_prof_valid = false;
    s_nvs_ver_valid  = false;
    s_nvs_schema_ver = 0U;
    settings_load_subtree("calib");

    if (s_nvs_ver_valid && (s_nvs_schema_ver != (uint8_t)CALIB_SCHEMA_VER)) {
        LOG_WRN("Calibration schema mismatch (stored=%u current=%u) — discarding",
                s_nvs_schema_ver, CALIB_SCHEMA_VER);
        s_nvs_prof_valid = false;
    } else if (!s_nvs_ver_valid && s_nvs_prof_valid) {
        LOG_WRN("Calibration has no version key — discarding old data");
        s_nvs_prof_valid = false;
    }

    if (s_nvs_prof_valid) {
        (void)memcpy(profiles_out, s_nvs_prof, sizeof(s_nvs_prof));
        (void)memcpy(s_profiles,   s_nvs_prof, sizeof(s_profiles));
        LOG_INF("Per-gesture profiles loaded from NVS (schema v%u)", s_nvs_schema_ver);
        for (uint8_t g = 0U; g < CALIB_NUM_GESTURES; g++) {
            LOG_INF("  %s: %u %u %u %u", s_phase_names[g],
                    s_profiles[g][0], s_profiles[g][1],
                    s_profiles[g][2], s_profiles[g][3]);
        }
        return 0;
    }

    LOG_INF("No saved calibration — using defaults");
    return -ENODATA;
}

int calibration_save(const uint16_t profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS])
{
    int err = settings_save_one(SETTINGS_KEY_PROF5, profiles,
                                sizeof(uint16_t) * CALIB_NUM_GESTURES * CALIB_NUM_FINGERS);
    if (err != 0) {
        LOG_ERR("calibration_save prof5 failed: %d", err);
        return err;
    }

    uint8_t ver = (uint8_t)CALIB_SCHEMA_VER;
    int err2 = settings_save_one(SETTINGS_KEY_VER, &ver, sizeof(ver));
    if (err2 != 0) {
        LOG_WRN("calibration_save ver failed: %d (non-fatal)", err2);
    }

    LOG_INF("Profiles saved (schema v%u):", CALIB_SCHEMA_VER);
    for (uint8_t g = 0U; g < CALIB_NUM_GESTURES; g++) {
        LOG_INF("  %s: %u %u %u %u", s_phase_names[g],
                profiles[g][0], profiles[g][1],
                profiles[g][2], profiles[g][3]);
    }
    return 0;
}

void calibration_start(void)
{
    (void)memset(s_sum, 0, sizeof(s_sum));
    s_count       = 0U;
    s_phase       = 0;
    s_collecting  = false;
    s_phase_start = k_uptime_get_32();
    LOG_INF("Calibration started: %u gestures × (prep=%u ms + sample=%u ms)",
            CALIB_NUM_GESTURES, CALIB_SETTLE_MS, CALIB_PHASE_MS);
}

int calibration_update(const uint16_t adc[CALIB_NUM_FINGERS],
                       uint16_t profiles_out[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS])
{
    if (s_phase < 0) {
        return -1;
    }

    uint32_t now            = k_uptime_get_32();
    uint32_t elapsed        = now - s_phase_start;
    bool     ready          = (elapsed >= CALIB_SETTLE_MS);
    uint32_t sample_elapsed = ready ? (elapsed - CALIB_SETTLE_MS) : 0U;

    if (!s_collecting && ready) {
        s_collecting = true;
        LOG_INF("Calibration %s: collecting for %u ms",
                s_phase_names[(uint8_t)s_phase], CALIB_PHASE_MS);
    }

    if (ready) {
        for (uint8_t i = 0U; i < CALIB_NUM_FINGERS; i++) {
            s_sum[i] += adc[i];
        }
        s_count++;
    }

    if (sample_elapsed >= CALIB_PHASE_MS) {
        /* Store average for current phase. */
        for (uint8_t i = 0U; i < CALIB_NUM_FINGERS; i++) {
            s_profiles[(uint8_t)s_phase][i] = (s_count > 0U)
                ? (uint16_t)(s_sum[i] / s_count)
                : 2048U;
        }
        LOG_INF("Phase %s done (n=%u): %u %u %u %u",
                s_phase_names[(uint8_t)s_phase], s_count,
                s_profiles[(uint8_t)s_phase][0], s_profiles[(uint8_t)s_phase][1],
                s_profiles[(uint8_t)s_phase][2], s_profiles[(uint8_t)s_phase][3]);

        int phase_done = s_phase + 1;  /* 1..5: which phase index just completed */
        s_phase++;

        if (s_phase >= (int8_t)CALIB_NUM_GESTURES) {
            s_phase = -1;
            (void)memcpy(profiles_out, s_profiles, sizeof(s_profiles));
            LOG_INF("Calibration complete: all %u gestures recorded", CALIB_NUM_GESTURES);
            return 10;  /* all done */
        }

        /* Advance to next phase: reset accumulators and timer. */
        (void)memset(s_sum, 0, sizeof(s_sum));
        s_count       = 0U;
        s_collecting  = false;
        s_phase_start = now;
        return phase_done;  /* 1..4: phase just done, starting next */
    }

    return 0;  /* in progress */
}

bool calibration_active(void)
{
    return s_phase >= 0;
}

const char *calibration_status_line(void)
{
    if (s_phase < 0 || s_phase >= (int8_t)CALIB_NUM_GESTURES) {
        return "Gata!";
    }
    return s_phase_names[(uint8_t)s_phase];
}

uint8_t calibration_current_phase(void)
{
    return (s_phase >= 0) ? (uint8_t)s_phase : 0U;
}
