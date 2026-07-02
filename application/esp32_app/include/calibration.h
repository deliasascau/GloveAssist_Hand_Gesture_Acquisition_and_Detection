/*
 * calibration.h - Per-gesture flex sensor calibration for GloveAssist.
 *
 * Calibrates each gesture individually in sequence:
 *   Phase 0: NONE  (all fingers open/extended — resting position)
 *   Phase 1: WATER (index bent, rest open)
 *   Phase 2: WC    (index + middle bent)
 *   Phase 3: FOOD  (index + middle + ring bent)
 *   Phase 4: HELP  (all bent — full fist)
 *
 * Each phase: CALIB_SETTLE_MS prep + CALIB_PHASE_MS sampling.
 * Profiles saved under NVS key "calib/prof5".
 *
 * Bump CALIB_SCHEMA_VER whenever profiles format or gesture set changes.
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>

#define CALIB_NUM_GESTURES  5U    /* NONE + 4 functional gestures             */
#define CALIB_NUM_FINGERS   4U    /* index, middle, ring, pinky               */
#define CALIB_SETTLE_MS  4000U    /* user prep time per gesture (3-2-1 @ 1.3s)*/
#define CALIB_PHASE_MS   5000U    /* effective sampling time per gesture       */
#define CALIB_SCHEMA_VER    5U    /* v5: per-gesture profiles (5 × 4 uint16)  */

/**
 * @brief Load saved per-gesture profiles from NVS.
 *
 * Must be called once during boot after settings_subsys_init().
 *
 * @param profiles_out  [CALIB_NUM_GESTURES][CALIB_NUM_FINGERS] — filled on success.
 * @return  0         : profiles loaded, profiles_out is valid.
 *          -ENODATA  : no saved data (calibrate first).
 *          other < 0 : settings subsystem error.
 */
int calibration_init(uint16_t profiles_out[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS]);

/**
 * @brief Persist per-gesture profiles to NVS flash.
 * @return 0 on success, negative errno on failure.
 */
int calibration_save(const uint16_t profiles[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS]);

/**
 * @brief Start a new calibration sequence (begins at phase 0: NONE).
 * Call calibration_update() every loop iteration while active.
 */
void calibration_start(void);

/**
 * @brief Feed current ADC values into the running calibration.
 *
 * @param adc           Current raw 12-bit ADC values [index, middle, ring, pinky].
 * @param profiles_out  Filled with all 5 gesture profiles when return == 10.
 * @return  0   : in progress.
 *          N   : phase N-1 just completed (N=1..5); next phase starting automatically.
 *          10  : all 5 phases done, profiles_out is valid — save + apply.
 *         -1   : not started (call calibration_start() first).
 */
int calibration_update(const uint16_t adc[CALIB_NUM_FINGERS],
                       uint16_t profiles_out[CALIB_NUM_GESTURES][CALIB_NUM_FINGERS]);

/** @return True while calibration is running. */
bool calibration_active(void);

/**
 * @brief Current gesture name being calibrated.
 * @return "NONE" / "WATER" / "WC" / "FOOD" / "HELP" / "Gata!" when idle.
 */
const char *calibration_status_line(void);

/** @return Index of the current calibration phase (0-4). */
uint8_t calibration_current_phase(void);

#endif /* CALIBRATION_H */
