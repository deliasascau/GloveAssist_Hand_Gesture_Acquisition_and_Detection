#include "boot.h"
#include "scheduler.h"
#include "led.h"
#include "adc.h"
#include "pwm.h"
#include "hm10.h"
#include "gestures.h"
#include "i2c.h"
#include "oled.h"
#include "eeprom_storage.h"
#include "project_config.h"

/**
 * @brief Init toate modulele hardware: LED→ADC→PWM→HM10→Gestures→I2C→OLED
 * @note  Ordine importantă pentru dependențe, total ~50-100ms
 */
static void init_hardware(void) {
    led_init();     // D3-D6 (iluminare LDR + indicator)
    adc_init();     // A0-A3 (citire LDR)
    buzzer_pwm_init();  // D11 (feedback audio)
    hm10_init();    // D8/D9 (BLE)
    gestures_init();
    i2c_init();     // A4-A5 (bus OLED)
    oled_init();    // 0x3C (display)
}

/**
 * @brief Splash screen: "GLOVE v2.1 Booting..." + beep 2000Hz 100ms, afișare 1s
 */
static void show_splash(void) {
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("GLOVE v2.1");
    oled_set_cursor(0, 2);
    oled_write_string("Booting...");
    oled_update();
    buzzer_beep(2000, 100);
    scheduler_delay_ms(1000);
}


/**
 * @brief Load profile gesturi din EEPROM + validare LDR
 * @return 1=succes (profile loaded + LDR OK), 0=EEPROM gol/invalid
 * @note  Afișează "Loading..." → gestures_load_from_eeprom() → double beep succes
 */
static uint8_t load_profiles_from_eeprom(void) {
    // Display loading message
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("Loading...");
    oled_update();
    scheduler_delay_ms(500);
    
    // NU mai trimitem mesaje debug prin BLE - doar OLED!
    
    // Load profiles
    if (!gestures_load_from_eeprom()) {
        // EEPROM gol - doar OLED feedback!
        
        oled_clear();
        oled_set_cursor(0, 0);
        oled_write_string("NO PROFILES!");
        oled_set_cursor(0, 2);
        oled_write_string("Hold fist");
        oled_set_cursor(0, 3);
        oled_write_string("20s to calibr");
        oled_update();
        
        // Long error beep (ton jos)
        buzzer_beep(500, 500);
        scheduler_delay_ms(3000);
        
        // Nu oprim sistemul - permitem calibrarea prin gest
        return 0;  // ✅ Returnăm 0 pentru NO PROFILES (înainte era 1 greșit!)
    }
    
    // Profile încărcate - doar OLED feedback!
    
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("LOADED!");
    oled_update();
    
    // Double beep confirmation (ton înalt)
    buzzer_beep(2500, 150);
    scheduler_delay_ms(100);
    buzzer_beep(2500, 150);
    scheduler_delay_ms(500);
    
    
    // Profile OK - skip LDR validation
    oled_clear();
    oled_set_cursor(0, 0);
    oled_write_string("PROFILES OK!");
    oled_update();
    buzzer_beep(3000, 100);
    scheduler_delay_ms(500);
    
    return 1;
}

/**
 * @brief Boot sistem: 1)Init HW, 2)Splash, 3)Load profiles EEPROM, 4)Ready
 * @return 1=succes boot (profile loaded), funcția nu returnează niciodată 0 (loop infinit la eroare)
 * @note  Flow: init_hardware()→show_splash()→load_profiles_from_eeprom()→LED ON→STATE_DETECTING
 */
uint8_t boot_system(void) {
    // Init Hardware
    init_hardware();
    
    // NU mai trimitem "BOOT:START" - prea verbose!
    
    // Splash Screen
    show_splash();
    
    // Load Profiles din EEPROM
    uint8_t profiles_loaded = load_profiles_from_eeprom();
    
    // NU mai trimitem status profiles - doar la final!
    
    // Ready State
    led_all_on();
    scheduler_delay_ms(100);
    
    oled_clear();
    oled_set_cursor(0, 0);
    
    if (!profiles_loaded) {
        // EEPROM gol - continuă dar afișează instrucțiuni calibrare
        oled_write_string("READY!");
        oled_set_cursor(0, 2);
        oled_write_string("Calibr: fist");
        oled_set_cursor(0, 3);
        oled_write_string("hold 20s");
    } else {
        // Profile încărcate
        oled_write_string("READY!");
        oled_set_cursor(0, 2);
        oled_write_string("Detecting...");
    }
    
    oled_update();
    
    // Set scheduler în modul DETECTING (task_detect_gesture va începe detecția)
    // Chiar dacă nu sunt profile, permitem detecția pentru a permite gestul de calibrare
    scheduler_set_state(STATE_DETECTING);
    
    // BLE: UN SINGUR MESAJ dacă totul e OK!
    if (profiles_loaded) {
        hm10_send_string("SYSTEM:OK\r\n");
    }
    // Dacă NU sunt profile, NU trimitem nimic prin BLE!
    
    return 1;  // Success - gata pentru detecție gesturi
}
