/**
 * @file app_secrets.h
 * @brief WiFi/MQTT credentials — DO NOT COMMIT (add to .gitignore)
 *
 * This file is #included by wifi_mqtt.c / any module that needs credentials.
 * Override app_config.h defaults using #define guards.
 */

#ifndef APP_SECRETS_H
#define APP_SECRETS_H

#define WIFI_SSID               "Delia's iPhone"
#define WIFI_PSK                "DN18062022"
#define ADAFRUIT_IO_USERNAME    "deliass"
#define ADAFRUIT_IO_KEY         "aio_bhTV24hKaOuQLZeee6zb1xXETAG7"

#endif /* APP_SECRETS_H */
