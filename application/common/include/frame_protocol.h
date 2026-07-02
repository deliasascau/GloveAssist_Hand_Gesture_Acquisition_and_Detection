/*
 * GloveAssist Frame Protocol - Shared between STM32 and ESP32
 *
 * Secure UART wire format (23 bytes):
 *   [SOF 0xAA][TYPE 1B][SEQ 1B][CTR32 4B][AES-CTR PAYLOAD 8B][HMAC TAG 8B]
 *
 * The 8-byte payload is encrypted with AES-128 in CTR mode.  The 8-byte tag is
 * the first bytes of HMAC-SHA256 over SOF+TYPE+SEQ+CTR32+CIPHERTEXT.
 */

#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Frame constants. */
#define FRAME_SOF          0xAAU
#define FRAME_PAYLOAD_LEN  8U
#define FRAME_COUNTER_LEN  4U
#define FRAME_SIZE         (1U + 1U + 1U + FRAME_COUNTER_LEN + FRAME_PAYLOAD_LEN)

/* Frame types. */
typedef enum {
    FRAME_TYPE_SENSOR_RAW  = 0x01U, /* STM32 -> ESP32: 4 x uint16 ADC values   */
    FRAME_TYPE_COMMAND     = 0x02U, /* ESP32 -> STM32: command + args           */
    FRAME_TYPE_STATUS      = 0x03U, /* bidirectional: ack / error status        */
    FRAME_TYPE_HEARTBEAT   = 0x04U, /* bidirectional: keepalive                 */
    FRAME_TYPE_SESSION     = 0x05U, /* bidirectional: HMAC-only session setup   */
} frame_type_t;

typedef enum {
    STATUS_ACK = 0xA5U,
    STATUS_ERR = 0xEEU,
} status_code_t;

/* Session handshake subtypes (payload[0] when type == FRAME_TYPE_SESSION). */
typedef enum {
    SESSION_HELLO = 0xC1U, /* ESP32 -> STM32: start/use session nonce */
    SESSION_ACK   = 0xC2U, /* STM32 -> ESP32: accepted session nonce  */
} session_msg_t;

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

/* Secure frame base.  payload[] is ciphertext on the wire and plaintext after
 * frame_validate_hmac() succeeds. */
typedef struct {
    uint8_t sof;                        /* always FRAME_SOF = 0xAA           */
    uint8_t type;                       /* frame_type_t                      */
    uint8_t seq;                        /* app sequence counter 0..255       */
    uint8_t counter[FRAME_COUNTER_LEN]; /* AES-CTR nonce counter, LE         */
    uint8_t payload[FRAME_PAYLOAD_LEN]; /* encrypted on wire, plain after RX */
} __attribute__((packed)) frame_t;

/* Compile-time size check */
typedef char _frame_size_check[(sizeof(frame_t) == FRAME_SIZE) ? 1 : -1];

/* -------------------------------------------------------------------------
 * AES-CTR + HMAC-SHA256 authenticated frame extension
 *
 * Wire format (23 bytes):
 *   [base frame 15B][HMAC-SHA256 tag 8B]
 *
 * AES-CTR uses a 7-byte product prefix, a 32-bit session nonce established by
 * HMAC-only handshake, a compile-time TX/RX role byte, and the 32-bit frame
 * counter.  ESP32 and STM32 compile the same source with opposite
 * FRAME_SECURE_TX_ID / FRAME_SECURE_RX_ID values, preventing keystream reuse
 * across directions.
 *
 * Security properties:
 *   - Confidentiality: payload is encrypted with AES-128-CTR.
 *   - Integrity + authenticity: HMAC-SHA256 tag covers metadata and ciphertext.
 *   - Replay resistance: 32-bit counter is rejected if it moves backwards
 *     within the current negotiated session.
 *
 * In production, replace both keys with device-unique provisioned secrets.
 * ------------------------------------------------------------------------- */
#define FRAME_AES_KEY_LEN      16U
#define FRAME_HMAC_KEY_LEN     32U
#define FRAME_HMAC_TAG_LEN      8U
#define FRAME_HMAC_SIZE        (FRAME_SIZE + FRAME_HMAC_TAG_LEN)  /* 23 bytes */

#ifndef FRAME_SECURE_TX_ID
#define FRAME_SECURE_TX_ID      0x00U
#endif

#ifndef FRAME_SECURE_RX_ID
#define FRAME_SECURE_RX_ID      0x00U
#endif

#ifndef FRAME_SECURE_ANTI_REPLAY
#define FRAME_SECURE_ANTI_REPLAY 1
#endif

#ifndef FRAME_AES_KEY
#define FRAME_AES_KEY \
    { 0x61U, 0xB4U, 0x09U, 0xCCU, 0x2EU, 0x87U, 0xF3U, 0x10U, \
      0x58U, 0xDAU, 0x76U, 0x4FU, 0x90U, 0x25U, 0xBEU, 0x1CU }
#endif

#ifndef FRAME_AES_NONCE_PREFIX
#define FRAME_AES_NONCE_PREFIX \
    { 0x47U, 0x4CU, 0x4FU, 0x56U, 0x45U, 0x41U, 0x45U }
#endif

#ifndef FRAME_HMAC_KEY
#define FRAME_HMAC_KEY \
    { 0x3AU, 0xF1U, 0x7CU, 0x55U, 0x9EU, 0xD2U, 0x4BU, 0x08U, \
      0xC6U, 0xEAU, 0x31U, 0x7DU, 0xB9U, 0x0FU, 0x42U, 0xA8U, \
      0x6DU, 0x5CU, 0x1EU, 0xF0U, 0x83U, 0x27U, 0xABU, 0x94U, \
      0xE7U, 0x60U, 0x3BU, 0xD5U, 0x1AU, 0x8FU, 0xCCU, 0x49U }
#endif

/* Extended frame: secure base (15 B) + truncated HMAC-SHA256 tag (8 B). */
typedef struct {
    frame_t base;
    uint8_t tag[FRAME_HMAC_TAG_LEN];
} __attribute__((packed)) frame_hmac_t;

/* Compile-time size check */
typedef char _frame_hmac_size_check[(sizeof(frame_hmac_t) == FRAME_HMAC_SIZE) ? 1 : -1];

/* API. */

/**
 * @brief Compute CRC-8/MAXIM (Dallas/Maxim, reflected) over a byte buffer.
 *
 * Kept for compatibility with older diagnostic code.  Secure UART frames use
 * HMAC-SHA256 instead of CRC for integrity.
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
 * @brief Build an HMAC-only session handshake frame.
 *
 * Session frames are authenticated but not encrypted because they establish
 * the session nonce used by subsequent AES-CTR frames.
 */
int frame_build_session(frame_hmac_t *out, uint8_t seq,
                        uint8_t msg, uint32_t session_nonce);

/** @brief Return the 32-bit counter/session nonce stored in a frame. */
uint32_t frame_get_counter(const frame_t *f);

/** @brief Reset negotiated secure session and replay counters. */
void frame_secure_reset_session(void);

/** @brief Set negotiated secure session and reset per-session counters. */
void frame_secure_set_session(uint32_t session_nonce);

/** @return True once encrypted frames can be sent/received. */
bool frame_secure_session_ready(void);

/**
 * @brief Build a plaintext base frame.
 * @param out     Output frame buffer.
 * @param type    Frame type (frame_type_t).
 * @param seq     Sequence number.
 * @param payload 8-byte payload array.
 * @return 0 on success, -1 on null pointer.
 */
int frame_build(frame_t *out, uint8_t type, uint8_t seq,
                const uint8_t *payload);

/**
 * @brief Validate a plaintext base frame.
 *
 * Secure UART users should call frame_validate_hmac(), which authenticates and
 * decrypts the payload before returning success.
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
 *         -1 = bad args, -2 = full frame but invalid.
 */
int frame_parser_push_byte(frame_parser_t *p, uint8_t byte, frame_t *out);

/* -------------------------------------------------------------------------
 * AES-CTR + HMAC-SHA256 authenticated frame API
 * ------------------------------------------------------------------------- */

/**
 * @brief Build an encrypted/authenticated frame.
 *
 * @param out     Output buffer (FRAME_HMAC_SIZE bytes).
 * @param type    Frame type (frame_type_t).
 * @param seq     Sequence number.
 * @param payload 8-byte plaintext payload.
 * @return 0 on success, -1 on null pointer.
 */
int frame_build_hmac(frame_hmac_t *out, uint8_t type, uint8_t seq,
                     const uint8_t *payload);

/**
 * @brief Validate HMAC, reject counter replay, then AES-CTR decrypt payload.
 *
 * Uses constant-time comparison for the tag to prevent timing oracles.
 * On success, f->base.payload contains plaintext values.
 *
 * @param f  Pointer to received secure frame (base modified in-place on success).
 * @return 0 if valid, -1 if HMAC or frame invalid.
 */
int frame_validate_hmac(frame_hmac_t *f);

/* Streaming HMAC-frame parser (byte-by-byte, same interface as frame_parser_t). */
typedef struct {
    uint8_t buffer[FRAME_HMAC_SIZE];
    uint8_t index;
    uint8_t active;
} frame_hmac_parser_t;

/** @brief Reset HMAC parser state. */
void frame_hmac_parser_init(frame_hmac_parser_t *p);

/**
 * @brief Push one received byte into HMAC parser.
 * @return 1 = valid HMAC frame, 0 = in progress, -1 = bad args,
 *         -2 = full frame but HMAC/decryption invalid.
 */
int frame_hmac_parser_push_byte(frame_hmac_parser_t *p, uint8_t byte,
                                frame_hmac_t *out);

#endif /* FRAME_PROTOCOL_H */
