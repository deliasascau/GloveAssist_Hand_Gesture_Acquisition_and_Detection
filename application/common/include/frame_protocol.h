/*
 * GloveAssist Frame Protocol - Shared between STM32 and ESP32
 *
 * Wire format (12 bytes):
 *   [SOF 0xAA][TYPE 1B][SEQ 1B][PAYLOAD 8B][CRC8 1B]
 *
 * CRC-8/MAXIM (Dallas/Maxim, reflected) covers:
 * TYPE + SEQ + PAYLOAD[8] = 10 bytes
 */

#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* Frame constants. */
#define FRAME_SOF        0xAAU
#define FRAME_SIZE       12U
#define FRAME_PAYLOAD_LEN 8U

/**
 * @brief Payload whitening key (XOR obfuscation layer on the UART wire).
 *
 * Wire encoding: wire_byte[i] = plaintext[i] ^ key[i] ^ seq
 * - key[i]  : per-position obfuscation (static, compile-time)
 * - seq      : rolling counter -> identical payloads produce different wire bytes
 *
 * CRC-8 is computed over the WHITENED bytes.  A forger must know this key to
 * produce a frame that passes CRC validation.
 *
 * Both STM32 and ESP32 must compile with the same FRAME_WHITENING_KEY.
 * In production replace with a device-unique provisioned key.
 */
#define FRAME_WHITENING_KEY \
    { 0x47U, 0xA3U, 0x1DU, 0x8FU, 0xC2U, 0x5BU, 0x6EU, 0x94U }

/* Frame types. */
typedef enum {
    FRAME_TYPE_SENSOR_RAW  = 0x01U, /* STM32 -> ESP32: 4 x uint16 ADC values   */
    FRAME_TYPE_COMMAND     = 0x02U, /* ESP32 -> STM32: command + args           */
    FRAME_TYPE_STATUS      = 0x03U, /* bidirectional: ack / error status        */
    FRAME_TYPE_HEARTBEAT   = 0x04U, /* bidirectional: keepalive                 */
} frame_type_t;

typedef enum {
    STATUS_ACK = 0xA5U,
    STATUS_ERR = 0xEEU,
} status_code_t;

/* Command subtypes (payload[0] when type == FRAME_TYPE_COMMAND). */
typedef enum {
    CMD_HAPTIC_BUZZ          = 0x01U, /* payload[1] = duration x10ms              */
    CMD_HAPTIC_VIBRATE       = 0x02U, /* payload[1] = duration x10ms              */
    CMD_OLED_TEXT            = 0x03U, /* payload[2..7] = 6-char ASCII text        */
    CMD_CALIBRATE            = 0x04U, /* trigger calibration sequence             */
    CMD_LED_ACK              = 0x05U, /* blink LED N times, payload[1] = count    */
    CMD_CAREGIVER_ACK        = 0x06U, /* caregiver said OK → STM32 "Understood"  */
    CMD_GESTURE_FEEDBACK     = 0x07U, /* payload[1] = gesture_id_t               */
    CMD_OLED_CALIB           = 0x08U, /* live calibration display:
                                       *   payload[1] = state (0-7):
                                       *     0-3: OPEN phase, step 3/2/1/TINE!
                                       *     4-7: FIST phase, step 3/2/1/TINE!
                                       *   payload[2] = index  ADC >> 4 (0-255)
                                       *   payload[3] = middle ADC >> 4
                                       *   payload[4] = ring   ADC >> 4
                                       *   payload[5] = pinky  ADC >> 4         */
} cmd_type_t;

/*
 * Gesture IDs — 4 functional gestures + meta values.
 *
 *   WATER  : index finger only extended  → request water/apă
 *   WC     : index + middle extended     → request toilet/WC
 *   FOOD   : index + middle + ring ext.  → request food/mâncare
 *   HELP   : full fist (all bent)        → emergency help/SOS
 *   CONFIRM: caregiver confirmation meta-event
 */
typedef enum {
    GESTURE_NONE    = 0x00U,
    GESTURE_WATER   = 0x01U, /* index pointing up           → "water/apă"    */
    GESTURE_WC      = 0x02U, /* V-sign (index+middle ext.)  → "WC/toilet"    */
    GESTURE_FOOD    = 0x03U, /* 3 fingers (idx+mid+ring)    → "food/mâncare" */
    GESTURE_HELP    = 0x04U, /* full fist                   → "help/SOS"     */
    GESTURE_CONFIRM = 0x05U, /* caregiver confirmation received               */
} gesture_id_t;

/* Wire frame structure. */
typedef struct {
    uint8_t sof;                     /* always FRAME_SOF = 0xAA              */
    uint8_t type;                    /* frame_type_t                         */
    uint8_t seq;                     /* rolling sequence counter 0..255      */
    uint8_t payload[FRAME_PAYLOAD_LEN]; /* type-specific payload             */
    uint8_t crc;                     /* CRC-8/MAXIM over type+seq+payload    */
} __attribute__((packed)) frame_t;

/* Compile-time size check */
typedef char _frame_size_check[(sizeof(frame_t) == FRAME_SIZE) ? 1 : -1];

/* API. */

/**
 * @brief Compute CRC-8/MAXIM (Dallas/Maxim, reflected) over a byte buffer.
 * @param data  Pointer to data.
 * @param len   Number of bytes.
 * @return CRC byte.
 */
uint8_t frame_crc8(const uint8_t *data, uint8_t len);

/**
 * @brief Build little-endian sensor payload from four 12-bit ADC values.
 * @param payload Output payload[8].
 * @param index   ADC value for index finger.
 * @param middle  ADC value for middle finger.
 * @param ring    ADC value for ring finger.
 * @param pinky   ADC value for pinky finger.
 */
void frame_make_sensor_payload(uint8_t payload[FRAME_PAYLOAD_LEN],
                               uint16_t index,
                               uint16_t middle,
                               uint16_t ring,
                               uint16_t pinky);

/**
 * @brief Decode little-endian payload into four ADC values.
 */
void frame_decode_sensor_payload(const uint8_t payload[FRAME_PAYLOAD_LEN],
                                 uint16_t *index,
                                 uint16_t *middle,
                                 uint16_t *ring,
                                 uint16_t *pinky);

/**
 * @brief Build a command payload.
 * @param payload Output payload[8].
 * @param cmd     Command subtype (cmd_type_t).
 * @param arg0    First argument (e.g. duration, count, gesture_id).
 * @param text    Optional 6-char text for CMD_OLED_TEXT (payload[2..7]).
 *                Pass NULL for non-text commands.
 */
void frame_make_cmd_payload(uint8_t payload[FRAME_PAYLOAD_LEN],
                            uint8_t cmd,
                            uint8_t arg0,
                            const char *text);

/**
 * @brief Build status payload where payload[0]=status and payload[1]=seq.
 */
void frame_make_status_payload(uint8_t payload[FRAME_PAYLOAD_LEN],
                               uint8_t status,
                               uint8_t seq);

/**
 * @brief Build a complete frame.
 * @param out     Output frame buffer.
 * @param type    Frame type (frame_type_t).
 * @param seq     Sequence number.
 * @param payload 8-byte payload array.
 * @return 0 on success, -1 on null pointer.
 */
int frame_build(frame_t *out, uint8_t type, uint8_t seq,
                const uint8_t *payload);

/**
 * @brief Validate SOF + CRC of a received frame, then dewhiten payload in-place.
 *
 * On success the frame's payload field contains PLAINTEXT values ready for
 * frame_decode_sensor_payload() etc.  The function modifies the frame in
 * place, so the pointer must be non-const.
 *
 * @param f  Pointer to received frame (modified on success).
 * @return 0 if valid, -1 if invalid.
 */
int frame_validate(frame_t *f);

/* Streaming parser API (UART byte-by-byte). */
typedef struct {
    uint8_t buffer[FRAME_SIZE];
    uint8_t index;
    uint8_t active;
} frame_parser_t;

/**
 * @brief Reset parser state.
 * @param p Parser state.
 */
void frame_parser_init(frame_parser_t *p);

/**
 * @brief Push one received byte into parser.
 * @param p    Parser state.
 * @param byte New byte from UART.
 * @param out  Output frame when a full frame is assembled.
 * @return 1 = valid frame completed, 0 = in progress/no frame,
 *         -1 = bad args, -2 = full frame but invalid (SOF/CRC).
 */
int frame_parser_push_byte(frame_parser_t *p, uint8_t byte, frame_t *out);

#endif /* FRAME_PROTOCOL_H */
