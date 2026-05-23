/**
 * @file app_config.h
 * @brief Application constants — sensor thresholds, timing, IDs
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "common_types.h"

/* ---------- ADC thresholds (12-bit, 0..4095) ----------
 *
 * Circuit: 3.3V --[1kΩ]--+-- PA_x (ADC)
 *                    [Senzor flex DIY ~600..1100Ω]
 *                        GND
 *
 * Deget drept  (senzor rezistență mare ~1100Ω): ADC ~ 3.3V*1100/(1000+1100) ≈ 2550 → bits ~3165
 * Deget îndoit (senzor rezistență mică  ~600Ω):  ADC ~ 3.3V*600/(1000+600)  ≈ 1238 → bits ~1537
 * Valoarea SCADE la îndoire → BENT < OPEN
 */
#define ADC_MIN_VALID          300U   /* sub 300 = senzor deconectat (open circuit)  */
#define ADC_MAX_VALID         3900U   /* peste 3900 = scurtcircuit                   */

/* ---------- Moving-average filter ---------- */
#define FILTER_WINDOW_SIZE       8U

/* ---------- Gesture detection ----------
 * Schema inversată: valoare mică = îndoit, valoare mare = drept
 * Calibrare reală (09-Apr-2026):
 *   Index : OPEN=1195, FIST=47   → prag=621
 *   Middle: OPEN=1695, FIST=53   → prag=874
 *   Ring  : OPEN=3671, FIST=347  → prag=2009
 *   Pinky : OPEN=1845, FIST=43   → prag=944
 */
#define GESTURE_HOLD_TIME_MS   300U
/* Praguri globale (fallback) */
#define GESTURE_THRESHOLD_BENT 1700U
#define GESTURE_THRESHOLD_OPEN 2300U
/* Praguri per-deget calibrate din date reale */
#define GESTURE_THRESH_INDEX    621U
#define GESTURE_THRESH_MIDDLE   874U
#define GESTURE_THRESH_RING    2009U
#define GESTURE_THRESH_PINKY    944U
#define GESTURE_NONE             0U
#define GESTURE_INDEX            1U
#define GESTURE_MIDDLE           2U
#define GESTURE_RING             3U
#define GESTURE_PINKY            4U
#define GESTURE_FIST             5U
#define GESTURE_HELP             6U
#define GESTURE_MAX_ID           6U

/* ---------- UART frame protocol (STM32 <-> ESP32) ----------
 *
 * Frame layout (PROTO_FRAME_SIZE = 12 bytes):
 *   [SOF 1B][TYPE 1B][SEQ 1B][PAYLOAD 8B obfuscated][CRC8 1B]
 *
 * XOR obfuscation: payload[i] ^= (SEQ ^ PROTO_XOR_KEY_BASE)
 * CRC-8/CCITT (poly 0x07): computed on plaintext bytes [TYPE..PAYLOAD]
 */
#define PROTO_SOF              0xAAU  /* Start-of-frame marker                  */
#define PROTO_PAYLOAD_SIZE       8U  /* Fixed payload — 4×uint8_t flex + meta  */
#define PROTO_FRAME_SIZE        12U  /* SOF+TYPE+SEQ+8×PAYLOAD+CRC8            */
#define PROTO_XOR_KEY_BASE     0x5AU /* XOR obfuscation base key (anti-sniff)  */
#define FRAME_POLL_INTERVAL_MS    100U /* STM32 sends a frame every 100 ms        */
#define GESTURE_REPEAT_TX_MS     2000U /* Re-send held gesture for app resync     */
#define STATUS_PUBLISH_INTERVAL_MS 10000U /* ESP32 cloud status publish interval */

/* frame_sensor_payload_t.status_flags bits */
#define SENSOR_STATUS_NEW_EVENT  (1U << 0)

/* ---------- Timing ---------- */
#define SENSOR_SAMPLE_PERIOD_MS  100U
#define SENSOR_IDLE_SAMPLE_PERIOD_MS 250U
#define SENSOR_IDLE_TIMEOUT_MS  3000U
#define OLED_REFRESH_MS          100U
#define WATCHDOG_TIMEOUT_MS     2000U

/* ---------- Heartbeat (Dual-Core Redundancy) ---------- */
#define HEARTBEAT_INTERVAL_MS    500U  /* ESP32 sends heartbeat every 500ms  */
#define HEARTBEAT_TIMEOUT_MS    2000U  /* STM32 triggers alarm after 2s      */
#define HEARTBEAT_GRACE_BOOTS      3U  /* Ignore first 3 missed at boot      */

/* ---------- Haptic patterns (motor + buzzer) ---------- */
#define HAPTIC_GESTURE_PULSE_MS  100U
#define HAPTIC_MSG_PULSE_MS      200U
#define HAPTIC_ERROR_PULSE_MS    150U
#define HAPTIC_SOS_PULSE_MS      300U  /* Long pulse for ESP32-down alarm    */

/* ---------- Thread priorities (lower number = higher prio) ---------- */
#define PRIO_SAFETY_THREAD         2   /* Highest — heartbeat monitor        */
#define PRIO_SECURITY_THREAD       3
#define PRIO_UART_COMM_THREAD        4
#define PRIO_SENSOR_THREAD         5
#define PRIO_BLE_THREAD            5
#define PRIO_HAPTIC_THREAD         6
#define PRIO_DISPLAY_THREAD        7
#define PRIO_WIFI_MQTT_THREAD      8   /* Below display — cloud publish is best-effort */

/* ---------- Thread stack sizes ---------- */
#define STACK_SAFETY             512U
#define STACK_SENSOR            2048U
#define STACK_HAPTIC            1024U
#define STACK_DISPLAY           1024U
#define STACK_UART_COMM          2048U
#define STACK_BLE               2048U
#define STACK_SECURITY          1024U

/* ---------- Security ---------- */
#define HONEYPOT_RATIO              3U
#define COUNTER_MAX_GAP             5U
#define KEY_ROTATION_SEC           10U
#define LOCKDOWN_INVALID_FRAMES     5U

/* ---------- WiFi credentials (fill before flashing) ---------- */
/* SECURITY: override these via prj_secrets.conf (not tracked in git) */
#ifndef WIFI_SSID
#define WIFI_SSID               "your-ssid-here"
#endif
#ifndef WIFI_PSK
#define WIFI_PSK                "your-password-here"
#endif
#define WIFI_CONNECT_TIMEOUT_S     15U

/* ---------- Adafruit IO / MQTT ---------- */
#define ADAFRUIT_IO_HOST        "io.adafruit.com"
#define ADAFRUIT_IO_PORT         8883U              /* MQTT over TLS */
#ifndef ADAFRUIT_IO_USERNAME
#define ADAFRUIT_IO_USERNAME    "your-username-here"
#endif
#ifndef ADAFRUIT_IO_KEY
#define ADAFRUIT_IO_KEY         "your-api-key-here"
#endif
#define MQTT_TLS_SEC_TAG          42
/*
 * Paste the current broker CA certificate before enabling WiFi/MQTT on hardware:
 *
 * #define ADAFRUIT_IO_CA_CERT_PEM \
 * "-----BEGIN CERTIFICATE-----\n" \
 * "...\n" \
 * "-----END CERTIFICATE-----\n"
 */
#define ADAFRUIT_IO_CA_CERT_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n" \
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n" \
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n" \
"MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n" \
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n" \
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n" \
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n" \
"2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n" \
"1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n" \
"q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n" \
"tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n" \
"vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n" \
"BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n" \
"5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n" \
"1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n" \
"NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n" \
"Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n" \
"8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n" \
"pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n" \
"MrY=\n" \
"-----END CERTIFICATE-----\n"

/* Feed topics: {username}/feeds/{feed} */
#define ADAFRUIT_IO_FEED_GESTURE  ADAFRUIT_IO_USERNAME "/feeds/glove.gesture"
#define ADAFRUIT_IO_FEED_STATUS   ADAFRUIT_IO_USERNAME "/feeds/glove.status"
#define MQTT_CLIENT_ID          "GloveAssist-ESP32"
#define MQTT_KEEPALIVE_S           60U
#define MQTT_PUBLISH_INTERVAL_MS  500U  /* min interval between publishes */

/* ---------- Thread stack — WiFi/MQTT (larger: network stack) ---------- */
/* TLS buffers live in MbedTLS heap; the thread stack only holds control flow. */
#define STACK_WIFI_MQTT         2048U

#endif /* APP_CONFIG_H */
