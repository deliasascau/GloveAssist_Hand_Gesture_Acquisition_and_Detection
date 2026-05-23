/**
 * @file frame_protocol.h
 * @brief Transport-agnostic 12-byte frame protocol used over UART.
 */

#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <stdint.h>

#include "app_config.h"

/* Message types */
#define MSG_TYPE_POLL        0x00U
#define MSG_TYPE_SENSOR_DATA 0x01U  /* Legacy: filtered + gesture (OLD arch) */
#define MSG_TYPE_GESTURE     0x02U  /* ESP32→STM32: detected gesture ID */
#define MSG_TYPE_ACK         0x03U
#define MSG_TYPE_STATUS      0x04U
#define MSG_TYPE_CALIBRATION 0x05U
#define MSG_TYPE_COMMAND     0x06U  /* ESP32→STM32: actuator commands */
#define MSG_TYPE_HEARTBEAT   0x07U  /* ESP32→STM32: keepalive */
#define MSG_TYPE_RAW_ADC     0x08U  /* STM32→ESP32: raw 12-bit ADC [NEW ARCH] */
#define MSG_TYPE_FILTERED    0x09U  /* ESP32→STM32: filtered values for OLED */
#define MSG_TYPE_HONEYPOT    0xFFU

/* Command IDs carried by MSG_TYPE_COMMAND */
#define CMD_CALIBRATE        0x01U
#define CMD_SET_THRESH       0x02U
#define CMD_RESET            0x03U
#define CMD_ACK_RECEIVED     0x10U  /* Phone → STM32: "inteles" → motor + OLED OK */
#define CMD_SNIFF_REPORT     0x70U

typedef struct __attribute__((packed)) {
    uint8_t sof;
    uint8_t type;
    uint8_t seq;
    uint8_t payload[PROTO_PAYLOAD_SIZE];
    uint8_t crc8;
} glove_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t flex[NUM_FLEX_SENSORS];
    uint8_t gesture_id;
    uint8_t confidence;
    uint8_t status_flags;
    uint8_t reserved;
} frame_sensor_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t ble_connected;
    uint8_t rssi;
    uint8_t reserved[5];
} frame_heartbeat_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t cmd_id;
    uint8_t finger_idx;
    uint16_t value;
    uint8_t reserved[4];
} frame_command_payload_t;

typedef struct __attribute__((packed)) {
    uint16_t raw[NUM_FLEX_SENSORS];
} frame_raw_adc_payload_t;

typedef struct __attribute__((packed)) {
    uint16_t filtered[NUM_FLEX_SENSORS];
} frame_filtered_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t gesture_id;
    uint8_t confidence;
    uint8_t hold_time_ms_h;
    uint8_t hold_time_ms_l;
    uint8_t reserved[4];
} frame_gesture_payload_t;

_Static_assert(sizeof(glove_frame_t) == PROTO_FRAME_SIZE,
               "glove_frame_t must be 12 bytes");
_Static_assert(sizeof(frame_sensor_payload_t) == PROTO_PAYLOAD_SIZE,
               "frame_sensor_payload_t must fit payload");
_Static_assert(sizeof(frame_heartbeat_payload_t) == PROTO_PAYLOAD_SIZE,
               "frame_heartbeat_payload_t must fit payload");
_Static_assert(sizeof(frame_command_payload_t) == PROTO_PAYLOAD_SIZE,
               "frame_command_payload_t must fit payload");
_Static_assert(sizeof(frame_raw_adc_payload_t) == PROTO_PAYLOAD_SIZE,
               "frame_raw_adc_payload_t must fit payload");
_Static_assert(sizeof(frame_filtered_payload_t) == PROTO_PAYLOAD_SIZE,
               "frame_filtered_payload_t must fit payload");
_Static_assert(sizeof(frame_gesture_payload_t) == PROTO_PAYLOAD_SIZE,
               "frame_gesture_payload_t must fit payload");

uint8_t crc8_ccitt(const uint8_t *data, uint32_t length);

int32_t frame_build(glove_frame_t *frame,
                    uint8_t msg_type,
                    const uint8_t *payload,
                    uint8_t payload_len);

int32_t frame_validate(const glove_frame_t *frame);

void frame_decode_payload(const glove_frame_t *frame,
                          uint8_t out[PROTO_PAYLOAD_SIZE]);

#endif /* FRAME_PROTOCOL_H */
