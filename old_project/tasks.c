// ============================================================================
// TASKS.C v2.5 - 3 Task-uri Optimizate + ML Demo
// ============================================================================
// T1: Display LDR (1Hz) → OLED live values + closest gesture
// T2: Detect Gesture (10Hz) → debounce 10×, LED full ON, drift check 60s
// T3: Process Gesture (on-demand) → multimodal feedback (OLED/LED/buzzer/BLE)
// T4: ML Demo (2Hz) → perceptron 4-letter recognition (A/B/C/D)
// Calibration: Fist hold 10s → auto-sequence 6 gestures → EEPROM save
// ============================================================================

#include "tasks.h"
#include "scheduler.h"
#include "adc.h"
#include "gestures.h"
#include "led.h"
#include "oled.h"
#include "pwm.h"
#include "hm10.h"
#include "eeprom_storage.h"
#include "ml_simple.h"

extern gesture_calibration_t g_calibration;

static uint8_t last_gesture = 0;
static uint8_t last_sent_gesture = 0;  // Ultimul gest TRIMIS prin BLE (pentru anti-duplicate)
static uint8_t stable_count = 0;
static uint8_t calibration_hold_count = 0;
// baseline_ldr și baseline_initialized - mutate în task_detect_gesture (local scope)

#define CALIBRATION_FIST_THRESHOLD 50

void task_display_ldr_values(void) {
    if (scheduler_get_state() != STATE_DETECTING) return;
    
    uint16_t ldr[4];
    for (uint8_t i = 0; i < 4; i++) ldr[i] = adc_read(i);
    
    uint16_t min_distance = 9999;
    uint8_t closest_gesture = 0;
    
    for (uint8_t g = 0; g < 6; g++) {
        if (!g_calibration.profiles[g].is_recorded) continue;
        uint16_t distance = 0;
        for (uint8_t i = 0; i < 4; i++) {
            int16_t diff = (int16_t)ldr[i] - (int16_t)g_calibration.profiles[g].ldr_values[i];
            if (diff < 0) diff = -diff;
            distance += diff;
        }
        if (distance < min_distance) {
            min_distance = distance;
            closest_gesture = g;
        }
    }
    
    char buf[20];
    const char* names[] = {"NON", "H2O", "WC", "FOOD", "SOS", "HI"};
    
    oled_clear();
    
    oled_set_cursor(0, 0);
    sprintf(buf, "1:%d 2:%d", ldr[0], ldr[1]);
    oled_write_string(buf);
    
    oled_set_cursor(0, 1);
    sprintf(buf, "3:%d 4:%d", ldr[2], ldr[3]);
    oled_write_string(buf);
    
    oled_set_cursor(0, 2);
    oled_write_string("-------------");
    
    oled_set_cursor(0, 3);
    sprintf(buf, "%s: %d", names[closest_gesture], min_distance);
    oled_write_string(buf);
    
    if (min_distance < 200 && stable_count > 0) {
        sprintf(buf, " %d/10", stable_count);
        oled_write_string(buf);
    } else if (min_distance < 200) {
        oled_write_string(" OK!");
    } else if (min_distance < 400) {
        oled_write_string(" ~");
    }
    
    
    oled_update();
}

void task_detect_gesture(void) {
    if (scheduler_get_state() != STATE_DETECTING) return;
    
    static uint16_t baseline_ldr[4] = {0};
    static uint8_t baseline_initialized = 0;
    
    // Init baseline la prima rulare (indiferent dacă avem profile sau nu!)
    // Baseline e necesar pentru adaptive calibration trigger
    if (!baseline_initialized) {
        for (uint8_t i = 0; i < 4; i++) baseline_ldr[i] = adc_read(i);
        baseline_initialized = 1;
        led_all_on();
    }
    
    led_all_on();

    
    // Check calibration fist gesture - ADAPTIVE THRESHOLD
    uint16_t ldr[4];
    
    // Citește LDR-uri curente
    for (uint8_t i = 0; i < 4; i++) {
        ldr[i] = adc_read(i);
    }
    
    // Adaptive trigger: TOTI 4 senzori < 50% din baseline (tolerant la LDR2 outlier)
    // Exemplu: baseline [664,113,638,385] - threshold [332,57,319,193]
    //          fist [4,3,75,4] - TOTI 4 senzori sub threshold (chiar si LDR2=75 < 319)
    uint8_t closed_count = 0;
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t threshold = baseline_ldr[i] / 2;  // 50% din baseline
        if (threshold < 30) threshold = 30;  // Safety: min 30 ADC units
        if (ldr[i] < threshold) {
            closed_count++;
        }
    }
    
    uint8_t all_fingers_closed = (closed_count == 4);  // TOTI 4 senzori închişi (PROFESIONAL!)
    
    if (all_fingers_closed) {
        calibration_hold_count++;
        
        // Progres calibration doar pe OLED (NU BLE!)
        // BLE e doar pentru gesturi finale!
        
        // SIMPLU: Fără beep-uri intermediare! Direct la calibrare la 20s
        if (calibration_hold_count >= 200) {  // 20 secunde @ 10Hz
            // NU mai trimitem prin BLE - doar OLED!
            
            // Un singur beep scurt
            buzzer_beep(2000, 50);
            
            oled_clear();
            oled_set_cursor(0, 0);
            oled_write_string("CALIBRATION");
            oled_set_cursor(0, 1);
            oled_write_string("MODE!");
            oled_update();
            
            scheduler_set_state(STATE_CALIBRATION);
            calibration_hold_count = 0;
            baseline_initialized = 0;
            task_calibrate_sequence();
            return;
        }
    } else {
        calibration_hold_count = 0;
    }
    
    // Normal gesture detection
    uint8_t gesture = gestures_detect();
    
    if (gesture == last_gesture && gesture != 0) {
        stable_count++;
        // Beep-uri de progres: la 0.6s, 1.2s, 1.8s (6, 12, 18)
        if (stable_count % 6 == 0 && stable_count < 20) {
            buzzer_beep(800 + (stable_count / 6) * 200, 40);
        }
        // Confirmă DOAR după 20× repetări = 2 secunde stabile!
        if (stable_count >= 20) {
            // VERIFICĂ dacă gestul a fost deja trimis!
            if (gesture != last_sent_gesture) {
                buzzer_beep(300, 150);  
                
                scheduler_set_state(STATE_GESTURE_FOUND);
            }
            stable_count = 0;  // Reset pentru următoarea detecție
        }
    } else {
        stable_count = 0;
        last_gesture = gesture;
        
        // RESETEAZĂ last_sent_gesture când gestul se schimbă (inclusiv la GESTURE_NONE)
        // Asta permite re-trimiterea aceluiași gest după ce s-a eliberat mâna
        if (gesture == 0) {
            last_sent_gesture = 0;
        }
    }
}

void task_process_gesture(void) {
    if (scheduler_get_state() != STATE_GESTURE_FOUND) return;
    if (last_gesture < 1 || last_gesture > 5) {
        last_gesture = 0;
        stable_count = 0;
        scheduler_set_state(STATE_DETECTING);
        return;
    }
    
    // Mesaje UMANE și SCURTE pentru BLE (mai prietenoase!)
    const char* ble_messages[] = {"", "H2O, please", "Toilet, please", "Food, please", "Help me!", "Question?"};
    const char* oled_names[] = {"", "WATER", "TOILET", "FOOD", "HELP", "QUESTION"};
    
    // BLE OUTPUT IMEDIAT (mesaj uman scurt!)
    char buf[40];
    sprintf(buf, "%s\r\n", ble_messages[last_gesture]);
    hm10_send_string(buf);
    
    // SALVEAZĂ gestul trimis pentru anti-duplicate!
    last_sent_gesture = last_gesture;
    
    // OLED display (nume clasic)
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string(oled_names[last_gesture]);
    oled_update();
    
    // LED blink RAPID (fără delay-uri lungi care cauzează probleme)
    uint8_t num_leds = (last_gesture == 5) ? 2 : last_gesture;
    for (uint8_t i = 0; i < num_leds; i++) led_set(i, 1);
    
    // Beep SCURT
    buzzer_beep(1000 + last_gesture * 400, 50);
    
    // LED OFF
    for (uint8_t i = 0; i < num_leds; i++) led_set(i, 0);
    
    // RESETEAZĂ last_gesture pentru a evita trimiteri multiple!
    last_gesture = 0;
    stable_count = 0;
    
    // FĂRĂ delay lung care blochează sistemul!
    scheduler_set_state(STATE_DETECTING);
}

void task_calibrate_sequence(void) {
    hm10_send_string("CALIBRATION:START\r\n");
    
    char buf[20];
    const char* names[] = {"NONE", "WATER", "TOILET", "FOOD", "HELP", "QUEST"};
    
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("CALIBRARE");
    oled_update();
    buzzer_beep(1500, 50);
    scheduler_delay_ms(500);  // Delay scurt
    
    for (uint8_t g = 0; g < 6; g++) {
        oled_clear();
        oled_set_cursor(0, 0);
        sprintf(buf, "Gest %d/6", g + 1);
        oled_write_string(buf);
        oled_set_cursor(0, 1);
        oled_write_string(names[g]);
        oled_update();
        scheduler_delay_ms(1000);  // 1s pentru citire
        
        // Countdown 3-2-1
        for (uint8_t c = 3; c > 0; c--) {
            oled_set_cursor(0, 2);
            sprintf(buf, "Gata: %d", c);
            oled_write_string(buf);
            oled_update();
            buzzer_beep(1000, 50);
            scheduler_delay_ms(800);
        }
        
        oled_set_cursor(0, 3);
        oled_write_string("TINE!");
        oled_update();
        buzzer_beep(2500, 50);
        
        gestures_record_profile(g);
        
        // NU mai trimitem date calibrare prin BLE - doar OLED!
        // BLE = DOAR gesturi finale!
        
        oled_clear();
        oled_set_cursor(0, 1);
        oled_write_string("OK!");
        oled_update();
        buzzer_beep(3000, 50);
        scheduler_delay_ms(500);
    }
    
    
    gestures_calibrate();  // Set is_calibrated = 1
    
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("Saving...");
    oled_update();
    
    uint8_t save_result = gestures_save_to_eeprom();
    
    if (save_result) {
        hm10_send_string("EEPROM:SAVE_OK\r\n");
        oled_set_cursor(0, 1);
        oled_write_string("SAVED!");
        buzzer_beep(3500, 50);
    } else {
        hm10_send_string("EEPROM:SAVE_FAIL\r\n");
        oled_set_cursor(0, 1);
        oled_write_string("FAIL!");
        buzzer_beep(500, 100);
    }
    oled_update();
    scheduler_delay_ms(1000);
    
    hm10_send_string("SYSTEM:READY_FOR_DETECTION\r\n");
    
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("CALIBRARE");
    oled_set_cursor(0, 1);
    oled_write_string("COMPLETA!");
    oled_update();
    
    buzzer_beep(2000, 50);
    scheduler_delay_ms(1000);
    
    scheduler_set_state(STATE_DETECTING);
    last_gesture = 0;
    stable_count = 0;
    calibration_hold_count = 0;
}

// ============================================================================
// DEBUG TASKS (COMMENTED OUT - Not used in production)
// ============================================================================

/*
// Debug: Display EEPROM profiles (for debugging)
void task_debug_display_eeprom(void) {
    gesture_profile_t profiles[6];
    char buf[20];
    const char* names[] = {"NONE", "WATER", "TOILET", "FOOD", "HELP", "QUEST"};
    
    uint16_t magic;
    eeprom_read_block(&magic, (const void*)0x00, sizeof(uint16_t));
    
    oled_clear();
    oled_set_cursor(0, 0);
    sprintf(buf, "Magic: 0x%04X", magic);
    oled_write_string(buf);
    oled_set_cursor(0, 1);
    oled_write_string((magic == 0x4750) ? "Status: VALID" : "Status: INVALID");
    oled_update();
    for (volatile uint32_t d = 0; d < 3200000; d++);
    
    uint8_t loaded = eeprom_load_profiles(profiles);
    if (!loaded) {
        oled_clear();
        oled_set_cursor(0, 1);
        oled_write_string("EEPROM EMPTY!");
        oled_update();
        for (volatile uint32_t d = 0; d < 3200000; d++);
        return;
    }
    
    for (uint8_t g = 0; g < 6; g++) {
        oled_clear();
        oled_set_cursor(0, 0);
        sprintf(buf, "%d:%s", g, names[g]);
        oled_write_string(buf);
        oled_set_cursor(0, 1);
        oled_write_string(profiles[g].is_recorded ? "REC" : "EMPTY");
        oled_set_cursor(0, 2);
        sprintf(buf, "1:%d 2:%d", profiles[g].ldr_values[0], profiles[g].ldr_values[1]);
        oled_write_string(buf);
        oled_set_cursor(0, 3);
        sprintf(buf, "3:%d 4:%d", profiles[g].ldr_values[2], profiles[g].ldr_values[3]);
        oled_write_string(buf);
        oled_update();
        buzzer_beep(1000 + g * 200, 100);
        for (volatile uint32_t d = 0; d < 4800000; d++);
    }
    
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("EEPROM DEBUG");
    oled_set_cursor(0, 1);
    oled_write_string("COMPLETE!");
    oled_update();
    buzzer_beep(2000, 200);
    for (volatile uint32_t d = 0; d < 1600000; d++);
}

void task_debug_compare_values(void) {
    gesture_profile_t profiles[6];
    char buf[20];
    const char* names[] = {"NONE", "WATER", "TOILET", "FOOD", "HELP", "QUEST"};
    
    if (!eeprom_load_profiles(profiles)) {
        oled_clear();
        oled_set_cursor(0, 1);
        oled_write_string("NO EEPROM DATA!");
        oled_update();
        for (volatile uint32_t d = 0; d < 3200000; d++);
        return;
    }
    
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("DETECTING...");
    oled_update();
    for (volatile uint32_t d = 0; d < 1600000; d++);
    
    led_all_on();
    uint16_t current[4];
    for (uint8_t i = 0; i < 4; i++) current[i] = adc_read(i);
    
    for (uint8_t g = 0; g < 6; g++) {
        if (!profiles[g].is_recorded) continue;
        
        uint16_t distance = 0;
        for (uint8_t i = 0; i < 4; i++) {
            int16_t diff = (int16_t)current[i] - (int16_t)profiles[g].ldr_values[i];
            if (diff < 0) diff = -diff;
            distance += diff;
        }
        
        oled_clear();
        oled_set_cursor(0, 0);
        sprintf(buf, "%s (dist:%d)", names[g], distance);
        oled_write_string(buf);
        oled_set_cursor(0, 1);
        oled_write_string("Curr  Saved");
        oled_set_cursor(0, 2);
        sprintf(buf, "%d   %d", current[0], profiles[g].ldr_values[0]);
        oled_write_string(buf);
        oled_set_cursor(0, 3);
        sprintf(buf, "%d   %d", current[1], profiles[g].ldr_values[1]);
        oled_write_string(buf);
        oled_update();
        buzzer_beep(1000 + g * 200, 100);
        for (volatile uint32_t d = 0; d < 3200000; d++);
    }
    
    oled_clear();
    oled_set_cursor(0, 1);
    oled_write_string("DEBUG DONE!");
    oled_update();
    buzzer_beep(2000, 200);
    for (volatile uint32_t d = 0; d < 1600000; d++);
}
*/

// ============================================================================
// TASK 4: ML DEMO - Machine Learning Proof-of-Concept (COMMENTED OUT)
// ============================================================================
/*
void task_ml_demo(void) {
	static char last_predicted = '\0';  // Pentru debounce
	static uint8_t stable_predictions = 0;
	
	// 1. Citește valorile LDR
	uint16_t ldr_values[4];
	ldr_values[0] = adc_read(0);
	ldr_values[1] = adc_read(1);
	ldr_values[2] = adc_read(2);
	ldr_values[3] = adc_read(3);
	
	// 2. Rulează ML inference
	uint8_t confidence = 0;
	char predicted_letter = ml_predict_with_confidence(ldr_values, &confidence);
	
	// 3. Debounce: cere 3 predicții consecutive identice (1.5s @ 2Hz)
	if (predicted_letter == last_predicted) {
		stable_predictions++;
	} else {
		stable_predictions = 0;
		last_predicted = predicted_letter;
	}
	
	// 4. Dacă avem predicție stabilă + confidence >50%, afișează feedback
	if (stable_predictions >= 3 && confidence >= 50) {
		// Reset debounce
		stable_predictions = 0;
		
		// OLED feedback
		oled_clear();
		oled_set_cursor(10, 0);
		oled_write_string("ML DEMO");
		
		oled_set_cursor(0, 1);
		oled_write_string("Letter: ");
		oled_write_char(predicted_letter);
		
		oled_set_cursor(0, 2);
		oled_write_string("Confidence: ");
		oled_print_value(confidence);
		oled_write_char('%');
		
		// Afișează LDR values pentru debugging
		oled_set_cursor(0, 3);
		// Folosim implementarea bare-metal din oled.c
		oled_write_string("LDR:");
		for (uint8_t i = 0; i < 4; i++) {
			oled_write_char(' ');
			oled_print_value(ldr_values[i]);
		}
		
		oled_update();
		
		// Buzzer feedback (frecvență bazată pe literă)
		uint16_t freq = 1000 + ((predicted_letter - 'A') * 400);  // A=1000Hz, B=1400Hz, C=1800Hz, D=2200Hz
		buzzer_beep(freq, 150);
		
		// LED feedback (blink pattern bazat pe literă)
		uint8_t blinks = (predicted_letter - 'A') + 1;  // A=1 blink, B=2, C=3, D=4
		for (uint8_t i = 0; i < blinks; i++) {
			led_set(0, 1);  // LED principal ON
			_delay_ms(100);
			led_set(0, 0);  // LED principal OFF
			_delay_ms(100);
		}
		
		// BLE feedback
		hm10_send_string("ML:");
		hm10_send_byte(predicted_letter);
		hm10_send_string(" (");
		hm10_send_byte('0' + (confidence / 10));      // Zeci
		hm10_send_byte('0' + (confidence % 10));      // Unități
		hm10_send_string("%)\r\n");
		
		// Pauză anti-redetectare (1s)
		_delay_ms(1000);
	}
	
	// 5. Afișare continuă pe OLED (mod monitoring)
	// Actualizează doar dacă confidence >30% (evită flickering pe zgomot)
	if (confidence >= 30) {
		oled_clear();
		oled_set_cursor(5, 0);
		oled_write_string("ML MONITORING");
		
		oled_set_cursor(0, 1);
		oled_write_string("Pred: ");
		oled_write_char(predicted_letter);
		oled_write_string("  Conf: ");
		oled_print_value(confidence);
		oled_write_char('%');
		
		oled_set_cursor(0, 2);
		oled_write_string("LDR: ");
		for (uint8_t i = 0; i < 4; i++) {
			oled_print_value(ldr_values[i] / 10);  // Afișează doar 2 cifre pentru spațiu
			oled_write_char(' ');
		}
		
		oled_set_cursor(0, 3);
		oled_write_string("Stable: ");
		oled_print_value(stable_predictions);
		oled_write_string("/3");
		
		oled_update();
	}
}
*/

