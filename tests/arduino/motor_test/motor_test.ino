/**
 * motor_test.ino
 * Test motor vibrații DC pe ATmega2560 (Arduino Mega)
 *
 * Conexiuni identice cu firmware STM32 (haptic_ui.c):
 *
 *   Pin 9 ──────────────── ULN2003 IN1
 *   GND ─────────────────── ULN2003 GND
 *   5V ──────────────────── ULN2003 COM (alimentare motor)
 *   ULN2003 OUT1 ─────────── Motor (−)
 *   5V ──────────────────── Motor (+)
 *
 * ULN2003 e low-side driver: cand IN1=HIGH, OUT1 trage la GND → motor pornit.
 * PWM pe Pin 9: analogWrite(9, 0..255) controlează viteza.
 *
 * NOTA: daca motorul nu porneste la duty mic, creste MIN_DUTY.
 *       Motoarele de vibrare tipice pornesc la ~120/255 (~47%).
 */

#define MOTOR_PIN   9    // PWM pin — schimba daca e necesar

/* Duty cycle 0..255 */
#define DUTY_FULL   200U  // putere normala (78%)
#define DUTY_STRONG 255U  // SOS / alarma maxima
#define DUTY_LOW    120U  // minim sigur de pornire

/* Durate (ms) — aceleasi ca in haptic_ui.c */
#define DUR_SHORT   100U
#define DUR_MEDIUM  200U
#define DUR_LONG    300U

/* ------------------------------------------------------------------ */
/*  Motor primitives — echivalent motor_pulse() din haptic_ui.c       */
/* ------------------------------------------------------------------ */
static void motor_on(uint8_t duty = DUTY_FULL)
{
    analogWrite(MOTOR_PIN, duty);
}

static void motor_off()
{
    analogWrite(MOTOR_PIN, 0);
}

static void motor_pulse(uint32_t duration_ms, uint8_t duty = DUTY_FULL)
{
    motor_on(duty);
    delay(duration_ms);
    motor_off();
}

static void motor_gap(uint32_t gap_ms = 100U)
{
    motor_off();
    delay(gap_ms);
}

/* ------------------------------------------------------------------ */
/*  Pattern-uri din firmware (haptic_ui.c)                            */
/* ------------------------------------------------------------------ */

/* Confirmare gest recunoscut: 1 puls scurt */
static void pattern_gesture_ack()
{
    Serial.println(F("Pattern: Gesture ACK"));
    motor_pulse(DUR_SHORT);
}

/* Mesaj primit de la telefon: 2 pulsuri */
static void pattern_message()
{
    Serial.println(F("Pattern: Message RX"));
    motor_pulse(DUR_SHORT);
    motor_gap(80);
    motor_pulse(DUR_SHORT);
}

/* Eroare senzor: 3 pulsuri medii */
static void pattern_error()
{
    Serial.println(F("Pattern: Error"));
    for (int i = 0; i < 3; i++) {
        motor_pulse(DUR_MEDIUM);
        motor_gap(100);
    }
}

/* SOS (modul degradat — ESP32 down):
 * 3 scurt + 3 lung + 3 scurt, la putere maxima */
static void pattern_sos()
{
    Serial.println(F("Pattern: SOS (degraded mode)"));
    // 3 scurt
    for (int i = 0; i < 3; i++) { motor_pulse(DUR_SHORT,  DUTY_STRONG); motor_gap(80); }
    motor_gap(200);
    // 3 lung
    for (int i = 0; i < 3; i++) { motor_pulse(DUR_LONG,   DUTY_STRONG); motor_gap(80); }
    motor_gap(200);
    // 3 scurt
    for (int i = 0; i < 3; i++) { motor_pulse(DUR_SHORT,  DUTY_STRONG); motor_gap(80); }
}

/* Boot: un puls lung de confirmare */
static void pattern_boot()
{
    Serial.println(F("Pattern: Boot OK"));
    motor_pulse(300);
}

/* Test sweep duty cycle — gaseste minimul de pornire al motorului tau */
static void sweep_duty()
{
    Serial.println(F("Sweep duty: 50..255 (creste treptat)"));
    for (uint16_t d = 50; d <= 255; d += 15) {
        Serial.print(F("  duty = "));
        Serial.print(d);
        Serial.println(F("/255"));
        motor_on((uint8_t)d);
        delay(400);
        motor_off();
        delay(100);
    }
}

/* ------------------------------------------------------------------ */
void setup()
{
    pinMode(MOTOR_PIN, OUTPUT);
    motor_off();

    Serial.begin(115200);
    Serial.println(F("Motor DC Test — ATmega2560"));
    Serial.print(F("Pin motor: "));
    Serial.println(MOTOR_PIN);
    Serial.println(F("ULN2003: IN1=Pin9, COM=5V, OUT1=Motor(-)"));

    delay(500);
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

    /* 2. Message */
    pattern_message();
    delay(800);

    /* 3. Error */
    pattern_error();
    delay(800);

    /* 4. SOS (modul degradat) */
    pattern_sos();
    delay(1500);

    /* 5. Sweep duty — ruleaza o data, util la prima pornire */
    static bool sweep_done = false;
    if (!sweep_done) {
        sweep_duty();
        sweep_done = true;
        delay(1000);
    }
}
