#include "gestures.h"
#include "adc.h"
#include "eeprom_storage.h"
#include "project_config.h"

gesture_calibration_t g_calibration = {
    .is_calibrated = 0,
    .last_gesture_time = 0
};

static gesture_t g_last_detected_gesture = GESTURE_NONE;
static uint8_t g_gesture_stable_count = 0;
static uint8_t g_compose_count = 0;

static const uint8_t finger_to_adc_channel[NUM_FINGERS] = {
    LDR1_CHANNEL, LDR2_CHANNEL, LDR3_CHANNEL, LDR4_CHANNEL
};

static uint16_t read_finger_ldr(uint8_t finger_index) {
    if (finger_index >= NUM_FINGERS) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 3; i++) {
        sum += adc_read(finger_to_adc_channel[finger_index]);
        _delay_ms(2);
    }
    return (uint16_t)(sum / 3);
}

static uint16_t calculate_pattern_distance(const uint16_t current[NUM_FINGERS], 
                                           const gesture_profile_t* profile) {
    if (!profile->is_recorded) return 0xFFFF;
    uint16_t distance = 0;
    for (uint8_t i = 0; i < NUM_FINGERS; i++) {
        int16_t diff = (int16_t)current[i] - (int16_t)profile->ldr_values[i];
        distance += (uint16_t)(diff < 0 ? -diff : diff);
    }
    return distance;
}

void gestures_init(void) {
    for (uint8_t g = 0; g < 6; g++) {
        g_calibration.profiles[g].is_recorded = 0;
    }
    g_calibration.is_calibrated = 0;
    LED_DDR |= LED_ALL_MASK;
}


uint8_t gestures_record_profile(gesture_t gesture) {
    if (gesture >= 6) return 0;
    
    uint32_t sum[NUM_FINGERS] = {0};
    for (uint8_t sample = 0; sample < CALIBRATION_SAMPLES; sample++) {
        for (uint8_t finger = 0; finger < NUM_FINGERS; finger++) {
            sum[finger] += read_finger_ldr(finger);
        }
        _delay_ms(100);
    }
    
    for (uint8_t finger = 0; finger < NUM_FINGERS; finger++) {
        g_calibration.profiles[gesture].ldr_values[finger] = sum[finger] / CALIBRATION_SAMPLES;
    }
    g_calibration.profiles[gesture].is_recorded = 1;
    return 1;
}

uint8_t gestures_calibrate(void) {
    for (uint8_t g = 0; g < 6; g++) {
        if (!g_calibration.profiles[g].is_recorded) return 0;
    }
    g_calibration.is_calibrated = 1;
    return 1;
}

static uint8_t gestures_validate_profiles(void) {
    for (uint8_t g = 0; g < 6; g++) {
        if (!g_calibration.profiles[g].is_recorded) continue;
        for (uint8_t i = 0; i < NUM_FINGERS; i++) {
            uint16_t ldr = g_calibration.profiles[g].ldr_values[i];
            // Range valid: 0-1023 (10-bit ADC), dar acceptăm 0-1023 (nu 10-1010!)
            if (ldr > 1023) return 0; 
        }
    }
    
    // Verifică dacă TOATE profilurile au EXACT aceleași valori (EEPROM corupt 0xFF)
    uint16_t first = g_calibration.profiles[0].ldr_values[0];
    uint8_t all_same = 1;
    for (uint8_t g = 0; g < 6; g++) {
        for (uint8_t i = 0; i < NUM_FINGERS; i++) {
            if (g_calibration.profiles[g].ldr_values[i] != first) {
                all_same = 0;  // Găsit diferență → OK!
                break;
            }
        }
        if (!all_same) break;
    }
    
    // Dacă toate sunt identice (și != 0), probabil EEPROM corupt
    if (all_same && first != 0) return 0;  
    
    return 1;  }

uint8_t gestures_load_from_eeprom(void) {
    if (!eeprom_has_valid_data()) return 0;
    if (!eeprom_load_profiles(g_calibration.profiles)) return 0;
    if (!gestures_validate_profiles()) return 0;
    g_calibration.is_calibrated = 1;
    return 1;
}

uint8_t gestures_save_to_eeprom(void) {
    return eeprom_save_profiles(g_calibration.profiles);
}

// Calculează threshold adaptiv bazat pe separarea inter-class
static uint16_t calculate_adaptive_threshold(gesture_t best_gesture, uint16_t min_dist) {
    if (!g_calibration.is_calibrated || best_gesture >= 6) return PATTERN_MATCH_THRESHOLD;
    
    // Găsește a 2-a cea mai apropiată distanță (next best)
    uint16_t second_min_dist = 0xFFFF;
    uint16_t dummy[NUM_FINGERS];
    for (uint8_t i = 0; i < NUM_FINGERS; i++) {
        dummy[i] = adc_read(finger_to_adc_channel[i]);
    }
    
    for (uint8_t g = 0; g < 6; g++) {
        if (g == best_gesture) continue;
        uint16_t dist = calculate_pattern_distance(dummy, &g_calibration.profiles[g]);
        if (dist < second_min_dist) {
            second_min_dist = dist;
        }
    }
    
    // Threshold = halfway între best și next best (margin-based classification)
    // Formula: threshold = min_dist + (second_min - min_dist) / 2
    // Minimum safety: 30% din separare pentru robustețe anti-noise
    uint16_t separation = (second_min_dist > min_dist) ? (second_min_dist - min_dist) : 100;
    uint16_t adaptive_threshold = min_dist + (separation * 30) / 100;  // 30% margin
    
    // Clamp între 50 (anti-noise) și 400 (max range pentru LDR variance mare)
    if (adaptive_threshold < 50) adaptive_threshold = 50;
    if (adaptive_threshold > 400) adaptive_threshold = 400;
    
    return adaptive_threshold;
}

gesture_t gestures_detect(void) {
    // CRITICA: Dacă nu avem profile calibrate, returnăm GESTURE_NONE (nu GESTURE_ERROR)
    // Acest lucru permite detecția gestului de calibrare (fist 10s) chiar fără profile
    if (!g_calibration.is_calibrated) return GESTURE_NONE;
    
    uint16_t current_values[NUM_FINGERS];
    for (uint8_t i = 0; i < NUM_FINGERS; i++) {
        current_values[i] = read_finger_ldr(i);
    }
    
    gesture_t best = GESTURE_NONE;
    uint16_t min_dist = 0xFFFF;
    
    for (uint8_t g = 0; g < 6; g++) {
        uint16_t dist = calculate_pattern_distance(current_values, &g_calibration.profiles[g]);
        if (dist < min_dist) {
            min_dist = dist;
            best = (gesture_t)g;
        }
    }
    
    // Threshold adaptiv bazat pe inter-class separation
    uint16_t threshold = calculate_adaptive_threshold(best, min_dist);
    
    gesture_t current = (min_dist > threshold) ? GESTURE_NONE : best;
    
    if (current != g_last_detected_gesture) {
        g_last_detected_gesture = current;
        g_compose_count = 1;
        g_gesture_stable_count = 0;
        return GESTURE_NONE;
    }
    
    if (g_compose_count < GESTURE_COMPOSE_TICKS) {
        g_compose_count++;
        return GESTURE_NONE;
    }
    
    if (g_gesture_stable_count < 255) g_gesture_stable_count++;
    return (g_gesture_stable_count >= GESTURE_DEBOUNCE_COUNT) ? current : GESTURE_NONE;
}

uint8_t gestures_is_calibrated(void) {
    return g_calibration.is_calibrated;
}

const char* gestures_to_string(gesture_t gesture) {
    switch (gesture) {
        case GESTURE_WATER: return "APA";
        case GESTURE_TOILET: return "TOALETA";
        case GESTURE_FOOD: return "MANCARE";
        case GESTURE_HELP: return "AJUTOR";
        case GESTURE_QUESTION: return "CE FACI?";
        case GESTURE_CALIBRATING: return "CALIBRARE";
        case GESTURE_ERROR: return "EROARE";
        default: return "NIMIC";
    }
}

const char* gestures_to_ble_message(gesture_t gesture) {
    switch (gesture) {
        case GESTURE_WATER: return "WATER";
        case GESTURE_TOILET: return "TOILET";
        case GESTURE_FOOD: return "FOOD";
        case GESTURE_HELP: return "HELP";
        case GESTURE_QUESTION: return "WHATSUP";
        default: return "NONE";
    }
}

void gestures_reset_calibration(void) {
    for (uint8_t g = 0; g < 6; g++) {
        g_calibration.profiles[g].is_recorded = 0;
    }
    g_calibration.is_calibrated = 0;
    eeprom_clear_profiles();
}

uint16_t gestures_calculate_distance(const uint16_t current[4], uint8_t gesture_id) {
    if (gesture_id >= 6 || !g_calibration.profiles[gesture_id].is_recorded) {
        return 0xFFFF;
    }
    return calculate_pattern_distance(current, &g_calibration.profiles[gesture_id]);
}

uint8_t gestures_is_profile_recorded(uint8_t gesture_id) {
    if (gesture_id >= 6) return 0;
    return g_calibration.profiles[gesture_id].is_recorded;
}

