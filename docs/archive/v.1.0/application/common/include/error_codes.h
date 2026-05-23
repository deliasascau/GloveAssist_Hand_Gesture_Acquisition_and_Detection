/**
 * @file error_codes.h
 * @brief Standardized error codes for GloveAssist (IEC 61508 traceability)
 */

#ifndef ERROR_CODES_H
#define ERROR_CODES_H

typedef enum {
    /* ---- Sensor domain (0x10..0x1F) ---- */
    ERR_SENSOR_OK               = 0x00,
    ERR_SENSOR_OPEN_CIRCUIT     = 0x10,
    ERR_SENSOR_SHORT_CIRCUIT    = 0x11,
    ERR_SENSOR_DRIFT            = 0x12,
    ERR_SENSOR_ALL_FAULT        = 0x13,

    /* ---- Communication domain (0x20..0x2F) ---- */
    ERR_COMM_TX_FAIL             = 0x20,
    ERR_COMM_CRC_MISMATCH       = 0x21,
    ERR_COMM_TIMEOUT            = 0x22,
    ERR_COMM_FRAME_INVALID      = 0x23,

    /* ---- BLE domain (0x30..0x3F) ---- */
    ERR_BLE_DISCONNECTED        = 0x30,
    ERR_BLE_NOTIFY_FAIL        = 0x31,
    ERR_BLE_PAIRING_FAIL       = 0x32,

    /* ---- Safety domain (0x40..0x4F) ---- */
    ERR_WATCHDOG_RESET          = 0x40,
    ERR_STACK_OVERFLOW          = 0x41,
    ERR_ESP32_HEARTBEAT_LOST   = 0x42,

    /* ---- Security domain (0x50..0x5F) ---- */
    ERR_SECURITY_REPLAY         = 0x50,
    ERR_SECURITY_CRC_INJECT    = 0x51,
    ERR_SECURITY_COUNTER_GAP   = 0x52,
    ERR_SECURITY_LOCKDOWN      = 0x53
} error_code_t;

#endif /* ERROR_CODES_H */
