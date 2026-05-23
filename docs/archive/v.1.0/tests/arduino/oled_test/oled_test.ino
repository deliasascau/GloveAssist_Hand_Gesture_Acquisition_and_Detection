/**
 * oled_test.ino
 * Test OLED SSD1306 128x64 pe ATmega2560 (Arduino Mega)
 *
 * Conexiuni:
 *   OLED VCC  → 3.3V (sau 5V dacă modulul are regulator)
 *   OLED GND  → GND
 *   OLED SDA  → Pin 20 (SDA hardware I2C Mega)
 *   OLED SCL  → Pin 21 (SCL hardware I2C Mega)
 *
 * Librărie necesară: "Adafruit SSD1306" + "Adafruit GFX"
 *   Arduino IDE → Tools → Manage Libraries → caută "Adafruit SSD1306"
 *
 * Adresă I2C: 0x3C (jumper SDO la GND) — aceeași ca în firmware STM32
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1   // pin RESET negestionat, legat la 3.3V/5V
#define OLED_I2C_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ------------------------------------------------------------------ */
/*  Afișează un mesaj centrat + număr linie (pentru test rapid)        */
/* ------------------------------------------------------------------ */
static void show(const char *line1, const char *line2 = nullptr,
                 const char *line3 = nullptr)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println(line1);
    if (line2) { display.setCursor(0, 16); display.println(line2); }
    if (line3) { display.setCursor(0, 32); display.println(line3); }

    display.display();
}

/* ------------------------------------------------------------------ */
/*  Simulare gesturi — aceeași mapare ca în sensor_logic.c             */
/* ------------------------------------------------------------------ */
static const char *const GESTURE_NAMES[] = {
    "---",       // GESTURE_NONE
    "INDEX",     // GESTURE_INDEX
    "MIDDLE",    // GESTURE_MIDDLE
    "RING",      // GESTURE_RING
    "PINKY",     // GESTURE_PINKY
    "FIST",      // GESTURE_FIST
    "!! HELP !!" // GESTURE_HELP
};
#define NUM_GESTURES 7

/* ------------------------------------------------------------------ */
void setup()
{
    Serial.begin(115200);
    Serial.println(F("OLED Test — ATmega2560"));

    Wire.begin();

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println(F("EROARE: SSD1306 nu raspunde la 0x3C!"));
        Serial.println(F("Verifica cablajul SDA/SCL si tensiunea."));
        while (1) { ; }  // halt
    }

    Serial.println(F("SSD1306 detectat OK"));

    /* ---- Test 1: splash screen ---- */
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.println(F("GloveAssist"));
    display.setTextSize(1);
    display.setCursor(20, 40);
    display.println(F("OLED Test OK"));
    display.display();
    delay(2000);

    /* ---- Test 2: toate pixelii ON ---- */
    display.fillScreen(SSD1306_WHITE);
    display.display();
    delay(500);
    display.clearDisplay();
    display.display();
    delay(300);

    /* ---- Test 3: linii orizontale ---- */
    for (int y = 0; y < SCREEN_HEIGHT; y += 8) {
        display.drawFastHLine(0, y, SCREEN_WIDTH, SSD1306_WHITE);
    }
    display.display();
    delay(800);
}

/* ------------------------------------------------------------------ */
void loop()
{
    /* Ciclu prin toate gesturile (simulare firmware) */
    for (uint8_t g = 0; g < NUM_GESTURES; g++) {
        display.clearDisplay();

        /* Header */
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.print(F("Gest ["));
        display.print(g);
        display.println(F("]"));

        /* Gesture mare, centrat */
        display.setTextSize(2);
        int16_t x = (SCREEN_WIDTH  - (int)(strlen(GESTURE_NAMES[g]) * 12)) / 2;
        if (x < 0) x = 0;
        display.setCursor(x, 24);
        display.println(GESTURE_NAMES[g]);

        /* Bara de progres (g/6) */
        display.setTextSize(1);
        display.setCursor(0, 56);
        display.print(F("["));
        for (uint8_t i = 0; i < 6; i++) {
            display.print(i < g ? F("#") : F("."));
        }
        display.print(F("]"));

        display.display();

        Serial.print(F("Gest: "));
        Serial.println(GESTURE_NAMES[g]);

        delay(1200);
    }

    /* Test modul degradat (ESP32 down) — aceeași logică din safety_diag.c */
    show("!! NO LINK !!",
         "LOCAL ALARM",
         "ESP32 offline");
    Serial.println(F("Modul degradat afisat"));
    delay(2000);

    /* Test mesaj calibrare */
    show("CALIBRARE",
         "Tin degetele",
         "drepte...");
    delay(1500);
    show("CALIBRARE",
         "Tin degetele",
         "indoite...");
    delay(1500);
    show("CALIBRARE",
         "DONE  OK",
         "Praguri salvate");
    delay(1500);
}
