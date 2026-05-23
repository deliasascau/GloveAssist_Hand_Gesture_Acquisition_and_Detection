/**
 * @file calibration.h
 * @brief Calibrare automata praguri flex sensors cu stocare NVS
 *
 * Fluxul de calibrare:
 *   1. La pornire: calibration_init() incarca pragurile din NVS.
 *      Daca NVS e gol -> scrie defaults din app_config.h.
 *      Daca valorile ADC live difera >CALIB_DRIFT_PERCENT fata de NVS -> log warning.
 *
 *   2. La cerere (BLE CMD_CALIBRATE sau manual):
 *      calibration_start() face o sesiune interactiva:
 *        - 5s: masoara ADC cu degetele DREPTE  -> OPEN per canal
 *        - 5s: masoara ADC cu degetele INDOITE -> BENT per canal
 *        - calculeaza prag = (OPEN + BENT) / 2
 *        - scrie in NVS
 *        - actualizeaza k_thresh[] in sensor_logic la urmatorul ciclu
 *
 * NVS key: ID 10 = profil versionat cu magic/checksum.
 *          ID 1..3 sunt pastrate doar pentru migrare din format vechi.
 */

#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>
#include "common_types.h"

struct adc_dt_spec;

/* Procent maxim de deriva acceptabila inainte de warning recalibrare */
#define CALIB_DRIFT_PERCENT   20U
/* Durata masurare pentru o pozitie (ms) */
#define CALIB_MEASURE_MS    5000U
/* Numar de esantioane mediate per pozitie */
#define CALIB_NUM_SAMPLES     50U

/* NVS key IDs */
#define CALIB_NVS_ID_THRESH  1U   /* praguri curente uint16_t[4] */
#define CALIB_NVS_ID_OPEN    2U   /* referinte open  uint16_t[4] */
#define CALIB_NVS_ID_BENT    3U   /* referinte bent  uint16_t[4] */
#define CALIB_NVS_ID_PROFILE 10U  /* profil robust versionat + checksum */

/**
 * @brief Initializeaza subsistemul de calibrare.
 *
 * Deschide NVS filesystem, incarca pragurile salvate sau scrie defaults.
 * Trebuie apelata o singura data, inainte de first calibration_get_thresh().
 *
 * @return 0 la succes, negativ la eroare NVS.
 */
int calibration_init(void);

/**
 * @brief Returneaza pragul curent pentru un deget.
 *
 * @param finger  Index deget: FINGER_INDEX..FINGER_PINKY (din common_types.h)
 * @return Prag ADC 12-bit. Daca finger invalid -> returneaza GESTURE_THRESHOLD_BENT.
 */
uint16_t calibration_get_thresh(uint8_t finger);

/**
 * @brief Returneaza true daca calibrarea a fost facuta (NVS contine date reale).
 */
bool calibration_is_calibrated(void);

/**
 * @brief Salveaza manual un set de praguri in NVS.
 *
 * Actualizeza si cache-ul in-memory folosit de calibration_get_thresh().
 *
 * @param thresh  Array de NUM_FLEX_SENSORS valori uint16_t.
 * @return 0 la succes, negativ la eroare NVS.
 */
int calibration_save(const uint16_t thresh[NUM_FLEX_SENSORS]);

/**
 * @brief Seteaza si salveaza pragul pentru un singur deget.
 *
 * Folosit de BLE CMD_SET_THRESH.
 *
 * @param finger  FINGER_INDEX..FINGER_PINKY
 * @param value   Noua valoare ADC prag
 * @return 0 la succes, negativ la eroare.
 */
int calibration_save_one(uint8_t finger, uint16_t value);

/**
 * @brief Reseteaza pragurile la defaults din app_config.h si suprascrie NVS.
 *
 * @return 0 la succes, negativ la eroare NVS.
 */
int calibration_reset_to_defaults(void);

/**
 * @brief Request automatic calibration from another thread.
 *
 * The sensor thread owns the ADC channel descriptors, so BLE/UART command
 * handlers should call this non-blocking request instead of running
 * calibration_start() directly.
 */
void calibration_request_start(void);

/**
 * @brief Consume a pending automatic calibration request.
 *
 * @return true exactly once for each calibration_request_start() call.
 */
bool calibration_take_start_request(void);

/**
 * @brief Porneste o sesiune de calibrare automata interactiva.
 *
 * BLOCANTA — apeleaza din sensor_thread sau un thread dedicat.
 * Durata: ~10s + overhead ADC.
 *
 * Faza 1 (CALIB_MEASURE_MS): masoara ADC cu toate degetele DREPTE.
 * Faza 2 (CALIB_MEASURE_MS): masoara ADC cu toate degetele INDOITE.
 * Calculeaza prag = (open + bent) / 2, salveaza in NVS.
 *
 * @param adc_channels  Pointer la array de adc_dt_spec (din sensor_logic.c)
 * @return 0 la succes, negativ la eroare ADC/NVS.
 */
int calibration_start(const struct adc_dt_spec *adc_channels);

#endif /* CALIBRATION_H */
