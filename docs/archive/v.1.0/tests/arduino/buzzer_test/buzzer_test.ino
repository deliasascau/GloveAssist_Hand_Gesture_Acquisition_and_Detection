/**
 * buzzer_test.ino
 * Test buzzer PASIV pe ATmega2560 (Arduino Mega)
 *
 * Circuit identic cu cel din firmware STM32 (haptic_ui.c):
 *
 *   Pin 8 ─── [1kΩ] ─── 2N2222A Base
 *                        2N2222A Collector ─── Buzzer (−)
 *                        Buzzer (+) ─── 3.3V sau 5V
 *                        2N2222A Emitter ─── GND
 *
 *   SAU direct (fara tranzistor, pentru test rapid):
 *   Pin 8 ─── Buzzer (+)
 *   GND   ─── Buzzer (−)
 *   (curent ~20mA — acceptabil pentru test scurt)
 *
 * NOTA: tone() de pe Arduino genereaza semnal PWM software
 *       identic cu implementarea din haptic_ui.c (buzzer_beep).
 *
 * Pin buzzer: BUZZER_PIN — schimba dupa necesitate
 */

#define BUZZER_PIN  8   // orice pin digital

/* ---- Frecvente folosite in firmware (haptic_ui.c) ---- */
#define FREQ_GESTURE_ACK   2000U  // confirmare gest recunoscut
#define FREQ_SOS           2500U  // pattern SOS
#define FREQ_ERROR         1000U  // eroare (ton mai jos)
#define FREQ_CALIBRATION   3000U  // calibrare start/stop

/* ---- Durate (ms) ---- */
#define DUR_SHORT   100U
#define DUR_MEDIUM  200U
#define DUR_LONG    300U

/* ------------------------------------------------------------------ */
/*  Buzzer primitives — echivalent buzzer_beep() din haptic_ui.c      */
/* ------------------------------------------------------------------ */
static void beep(uint16_t freq_hz, uint16_t duration_ms)
{
    tone(BUZZER_PIN, freq_hz, duration_ms);
    delay(duration_ms + 20);  // +20ms gap intre sunete
    noTone(BUZZER_PIN);
}

static void beep_gap(uint16_t gap_ms = 100U)
{
    noTone(BUZZER_PIN);
    delay(gap_ms);
}

/* ------------------------------------------------------------------ */
/*  Pattern-uri din firmware (safety_diag.c + haptic_ui.c)            */
/* ------------------------------------------------------------------ */

/* 3 scurt + 3 lung + 3 scurt (SOS Morse) */
static void pattern_sos()
{
    Serial.println(F("Pattern: SOS"));
    // 3 scurt
    for (int i = 0; i < 3; i++) { beep(FREQ_SOS, DUR_SHORT); beep_gap(80); }
    beep_gap(200);
    // 3 lung
    for (int i = 0; i < 3; i++) { beep(FREQ_SOS, DUR_LONG);  beep_gap(80); }
    beep_gap(200);
    // 3 scurt
    for (int i = 0; i < 3; i++) { beep(FREQ_SOS, DUR_SHORT); beep_gap(80); }
}

/* Confirmare gest recunoscut: 1 bip scurt */
static void pattern_gesture_ack()
{
    Serial.println(F("Pattern: Gesture ACK"));
    beep(FREQ_GESTURE_ACK, DUR_SHORT);
}

/* Mesaj primit de la telefon: 2 bip-uri */
static void pattern_message()
{
    Serial.println(F("Pattern: Message RX"));
    beep(FREQ_GESTURE_ACK, DUR_SHORT);
    beep_gap(80);
    beep(FREQ_GESTURE_ACK, DUR_SHORT);
}

/* Eroare senzor: 3 bip-uri medii ton jos */
static void pattern_error()
{
    Serial.println(F("Pattern: Error"));
    for (int i = 0; i < 3; i++) {
        beep(FREQ_ERROR, DUR_MEDIUM);
        beep_gap(100);
    }
}

/* Boot complete: ton ascendent */
static void pattern_boot()
{
    Serial.println(F("Pattern: Boot OK"));
    beep(1000, 80); beep_gap(40);
    beep(1500, 80); beep_gap(40);
    beep(2000, 80); beep_gap(40);
    beep(2500, 120);
}

/* Calibrare start/stop */
static void pattern_calibration()
{
    Serial.println(F("Pattern: Calibration"));
    beep(FREQ_CALIBRATION, DUR_MEDIUM);
    beep_gap(100);
    beep(FREQ_CALIBRATION, DUR_MEDIUM);
}

/* ------------------------------------------------------------------ */
void setup()
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.begin(115200);
    Serial.println(F("Buzzer Pasiv Test — ATmega2560"));
    Serial.print(F("Pin buzzer: "));
    Serial.println(BUZZER_PIN);

    delay(500);

    /* Test rapid la pornire: confirmare initializare */
    pattern_boot();
    delay(500);
}

/* ------------------------------------------------------------------ */
void loop()
{
    Serial.println(F("\n=== Ciclu test pattern-uri ==="));

    /* 1. Gesture ACK */
    pattern_gesture_ack();
    delay(800);

    /* 2. Message notification */
    pattern_message();
    delay(800);

    /* 3. Error */
    pattern_error();
    delay(800);

    /* 4. Calibration */
    pattern_calibration();
    delay(800);

    /* 5. SOS (modul degradat — ESP32 down) */
    pattern_sos();
    delay(1500);

    /* 6. Sweep frecvente — verifica raspunsul acoustic al buzzerului */
    Serial.println(F("Sweep frecvente: 500Hz..4000Hz"));
    for (uint16_t f = 500; f <= 4000; f += 250) {
        Serial.print(F("  freq = "));
        Serial.print(f);
        Serial.println(F(" Hz"));
        beep(f, 150);
        beep_gap(50);
    }

    delay(2000);
}
