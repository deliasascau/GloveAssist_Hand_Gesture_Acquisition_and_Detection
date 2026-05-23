/**
 * @file common_types.h
 * @brief MISRA C:2012 compliant type definitions for GloveAssist
 *
 * Rule 4.6: typedefs shall be used in place of basic numerical types.
 */

#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Fixed-width type aliases (MISRA C:2012 Rule 4.6) */
typedef uint8_t   u8_t;
typedef uint16_t  u16_t;
typedef uint32_t  u32_t;
typedef int8_t    s8_t;
typedef int16_t   s16_t;
typedef int32_t   s32_t;
typedef float     f32_t;

/* Number of flex sensors on the glove */
#define NUM_FLEX_SENSORS  4U

/* Finger index mapping */
#define FINGER_INDEX   0U
#define FINGER_MIDDLE  1U
#define FINGER_RING    2U
#define FINGER_PINKY   3U

/* Sensor data bundle sent from STM32 -> ESP32 via UART */
typedef struct {
    u16_t flex_raw[NUM_FLEX_SENSORS];
    u16_t flex_filtered[NUM_FLEX_SENSORS];
    u8_t  gesture_id;
    u8_t  confidence;
    u8_t  status_flags;
    u32_t timestamp_ms;
} sensor_packet_t;

/* System-wide status codes */
typedef enum {
    SYS_OK                    = 0,
    SYS_ERROR_SENSOR_FAULT    = 1,
    SYS_ERROR_COMM_TIMEOUT     = 2,
    SYS_ERROR_BLE_FAILURE     = 3,
    SYS_ERROR_CALIBRATION     = 4,
    SYS_ERROR_SECURITY_BREACH = 5,
    SYS_DEGRADED_NO_ESP32     = 6   /* ESP32 heartbeat lost — local alarm */
} sys_status_t;

#endif /* COMMON_TYPES_H */
