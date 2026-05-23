/**
 * @file unit_tests.c
 * @brief Ztest unit tests for GloveAssist transport-agnostic modules
 *
 * Modules under test (no hardware drivers required):
 *   - frame_protocol.c : crc8_ccitt(), frame_build(), frame_validate()
 *
 * Build target: native_sim
 *   west build -b native_sim tests/
 *
 * Requirements covered:
 *   FR-007 â€” UART CRC frame integrity
 *   FR-008 â€” XOR obfuscation (anti-sniffing)
 *   FR-012 â€” SEQ rolling counter
 */

#include <zephyr/ztest.h>
#include <string.h>
#include "frame_protocol.h"
#include "app_config.h"
#include "common_types.h"
#include "error_codes.h"

/*
 * crc8_ccitt() is defined in frame_protocol.c; not exposed in the public header
 * (internal implementation detail). White-box declared here for direct testing.
 */
extern uint8_t crc8_ccitt(const uint8_t *data, uint32_t length);

/* ------------------------------------------------------------------ */
/*  CRC-8/CCITT tests  (poly=0x07, init=0x00)             FR-007      */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_crc8_known_vector)
{
    /* Published check value for "123456789" with CRC-8/CCITT is 0xF4 */
    const uint8_t data[] = {0x31U, 0x32U, 0x33U, 0x34U, 0x35U,
                             0x36U, 0x37U, 0x38U, 0x39U};
    uint8_t crc = crc8_ccitt(data, 9U);
    zassert_equal(crc, 0xF4U, "CRC-8 check vector mismatch: got 0x%02X", crc);
}

ZTEST(gloveassist, test_crc8_single_zero_byte)
{
    const uint8_t data[] = {0x00U};
    uint8_t crc = crc8_ccitt(data, 1U);
    zassert_equal(crc, 0x00U, "CRC-8 of 0x00 should be 0x00");
}

ZTEST(gloveassist, test_crc8_different_data_differ)
{
    const uint8_t dataA[] = {0xAAU, 0xBBU};
    const uint8_t dataB[] = {0xAAU, 0xBCU};  /* 1 bit different */
    uint8_t crcA = crc8_ccitt(dataA, 2U);
    uint8_t crcB = crc8_ccitt(dataB, 2U);
    zassert_not_equal(crcA, crcB, "Different data must produce different CRC");
}

ZTEST(gloveassist, test_crc8_deterministic)
{
    /* Same input must always yield the same CRC (no state leakage) */
    const uint8_t data[] = {0x01U, 0x07U, 0xAAU, 0x5AU, 0xFFU};
    uint8_t crc1 = crc8_ccitt(data, sizeof(data));
    uint8_t crc2 = crc8_ccitt(data, sizeof(data));
    zassert_equal(crc1, crc2, "CRC-8 must be deterministic");
}

/* ------------------------------------------------------------------ */
/*  frame_build() tests                                           */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_frame_build_null_frame)
{
    int32_t ret = frame_build(NULL, MSG_TYPE_POLL, NULL, 0U);
    zassert_true(ret < 0, "NULL frame pointer must return error");
}

ZTEST(gloveassist, test_frame_build_poll_sof)
{
    glove_frame_t frame;
    int32_t ret = frame_build(&frame, MSG_TYPE_POLL, NULL, 0U);
    zassert_equal(ret, 0, "Poll frame build failed");
    zassert_equal(frame.sof, (uint8_t)PROTO_SOF,
                  "SOF must always be 0x%02X", PROTO_SOF);
    zassert_equal(frame.type, (uint8_t)MSG_TYPE_POLL, "Type field mismatch");
}

ZTEST(gloveassist, test_frame_build_sets_correct_type)
{
    glove_frame_t frame;
    frame_build(&frame, MSG_TYPE_GESTURE, NULL, 0U);
    zassert_equal(frame.type, (uint8_t)MSG_TYPE_GESTURE,
                  "Type not stored correctly");
}

ZTEST(gloveassist, test_frame_size_is_12_bytes)
{
    /* _Static_assert in frame_protocol.h already checks this at compile time */
    zassert_equal(sizeof(glove_frame_t), (size_t)PROTO_FRAME_SIZE,
                  "glove_frame_t must be %u bytes, got %u",
                  PROTO_FRAME_SIZE, sizeof(glove_frame_t));
}

/* ------------------------------------------------------------------ */
/*  frame_validate() round-trip tests                  FR-007     */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_frame_roundtrip_gesture)
{
    glove_frame_t frame;
    uint8_t payload[PROTO_PAYLOAD_SIZE] = {10U, 20U, 30U, 40U,
                                            50U, 60U, 70U, 80U};

    int32_t ret = frame_build(&frame, MSG_TYPE_GESTURE,
                                  payload, (uint8_t)PROTO_PAYLOAD_SIZE);
    zassert_equal(ret, 0, "Frame build failed");

    int32_t valid = frame_validate(&frame);
    zassert_equal(valid, 0,
                  "Freshly built frame must pass validation (got %d)", valid);
}

ZTEST(gloveassist, test_frame_roundtrip_heartbeat)
{
    glove_frame_t frame;
    int32_t ret = frame_build(&frame, MSG_TYPE_HEARTBEAT, NULL, 0U);
    zassert_equal(ret, 0, "Heartbeat frame build failed");
    zassert_equal(frame_validate(&frame), 0,
                  "Heartbeat validation failed");
}

ZTEST(gloveassist, test_frame_roundtrip_sensor_data)
{
    glove_frame_t frame;
    frame_sensor_payload_t pkt;
    (void)memset(&pkt, 0, sizeof(pkt));
    pkt.flex[0]      = 200U;
    pkt.flex[1]      = 128U;
    pkt.flex[2]      = 64U;
    pkt.flex[3]      = 10U;
    pkt.gesture_id   = GESTURE_FIST;
    pkt.confidence   = 90U;
    pkt.status_flags = 0U;
    pkt.reserved     = 0U;

    int32_t ret = frame_build(&frame, MSG_TYPE_SENSOR_DATA,
                                  (const uint8_t *)&pkt,
                                  (uint8_t)sizeof(pkt));
    zassert_equal(ret, 0, "Sensor data frame build failed");
    zassert_equal(frame_validate(&frame), 0,
                  "Sensor data frame validation failed");
}

ZTEST(gloveassist, test_frame_corrupted_crc_fails)
{
    glove_frame_t frame;
    uint8_t payload[PROTO_PAYLOAD_SIZE] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    frame_build(&frame, MSG_TYPE_SENSOR_DATA, payload,
                    (uint8_t)PROTO_PAYLOAD_SIZE);

    frame.crc8 ^= 0xFFU;  /* flip all CRC bits â€” guaranteed mismatch */

    int32_t valid = frame_validate(&frame);
    zassert_not_equal(valid, 0, "Corrupted CRC must fail validation");
}

ZTEST(gloveassist, test_frame_corrupted_payload_fails)
{
    glove_frame_t frame;
    uint8_t payload[PROTO_PAYLOAD_SIZE] = {0xAAU, 0xBBU, 0xCCU, 0xDDU,
                                            0xEEU, 0xFFU, 0x11U, 0x22U};
    frame_build(&frame, MSG_TYPE_SENSOR_DATA, payload,
                    (uint8_t)PROTO_PAYLOAD_SIZE);

    frame.payload[0] ^= 0x01U;  /* flip 1 bit in wire payload */

    int32_t valid = frame_validate(&frame);
    zassert_not_equal(valid, 0, "Payload bit-flip must fail CRC validation");
}

ZTEST(gloveassist, test_frame_wrong_sof_fails)
{
    glove_frame_t frame;
    frame_build(&frame, MSG_TYPE_POLL, NULL, 0U);
    frame.sof = 0x00U;  /* corrupt SOF */

    int32_t valid = frame_validate(&frame);
    zassert_not_equal(valid, 0, "Wrong SOF must fail validation");
}

ZTEST(gloveassist, test_frame_corrupted_type_fails)
{
    glove_frame_t frame;
    frame_build(&frame, MSG_TYPE_GESTURE, NULL, 0U);
    /* Corrupt TYPE after build â€” CRC was computed over the original type */
    frame.type = MSG_TYPE_HONEYPOT;

    int32_t valid = frame_validate(&frame);
    zassert_not_equal(valid, 0,
                      "Corrupted TYPE field must fail CRC validation");
}

/* ------------------------------------------------------------------ */
/*  SEQ rolling counter tests                              FR-012      */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_seq_increments_per_frame)
{
    glove_frame_t frameA, frameB;
    frame_build(&frameA, MSG_TYPE_POLL, NULL, 0U);
    frame_build(&frameB, MSG_TYPE_POLL, NULL, 0U);
    zassert_not_equal(frameA.seq, frameB.seq,
                      "SEQ counter must increment between consecutive builds");
}

ZTEST(gloveassist, test_seq_wraps_at_256)
{
    /*
     * Drive the counter through a full 256-cycle wrap.
     * 256 builds after recording the baseline must land on the same SEQ.
     */
    glove_frame_t f;
    uint8_t first_seq;
    uint32_t i;

    frame_build(&f, MSG_TYPE_POLL, NULL, 0U);
    first_seq = f.seq;

    for (i = 0U; i < 255U; i++) {
        frame_build(&f, MSG_TYPE_POLL, NULL, 0U);
    }

    frame_build(&f, MSG_TYPE_POLL, NULL, 0U);
    zassert_equal(f.seq, first_seq,
                  "SEQ must wrap: expected %u got %u", first_seq, f.seq);
}

/* ------------------------------------------------------------------ */
/*  XOR obfuscation tests                                  FR-008      */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_xor_roundtrip_validate)
{
    /*
     * frame_build() XOR-obfuscates the payload on the wire.
     * frame_validate() reverses the XOR and checks the CRC.
     * A freshly built frame must always pass validation.
     */
    glove_frame_t frame;
    const uint8_t original[PROTO_PAYLOAD_SIZE] =
        {0x80U, 0x60U, 0x40U, 0x20U, 0x06U, 0x64U, 0x00U, 0xFFU};

    (void)frame_build(&frame, MSG_TYPE_GESTURE, original,
                          (uint8_t)PROTO_PAYLOAD_SIZE);

    int32_t ret = frame_validate(&frame);
    zassert_equal(ret, 0, "XOR encodeâ†’decode round-trip failed: %d", ret);
}

ZTEST(gloveassist, test_xor_wire_differs_from_plaintext)
{
    /*
     * For a non-zero XOR key (SEQ ^ 0x5A != 0), at least one wire byte
     * must differ from the original plaintext.
     * Using 0xFF payload maximises probability of visible XOR change.
     */
    glove_frame_t frame;
    uint8_t plaintext[PROTO_PAYLOAD_SIZE];
    uint32_t i;
    uint32_t differences = 0U;

    (void)memset(plaintext, 0xFFU, sizeof(plaintext));
    frame_build(&frame, MSG_TYPE_SENSOR_DATA, plaintext,
                    (uint8_t)PROTO_PAYLOAD_SIZE);

    for (i = 0U; i < (uint32_t)PROTO_PAYLOAD_SIZE; i++) {
        if (frame.payload[i] != plaintext[i]) {
            differences++;
        }
    }

    /* Edge case: key == 0 when SEQ == PROTO_XOR_KEY_BASE. Skip assert then. */
    uint8_t xor_key = (uint8_t)(frame.seq ^ (uint8_t)PROTO_XOR_KEY_BASE);
    if (xor_key != 0U) {
        zassert_true(differences > 0U,
                     "Non-zero XOR key must change at least one wire byte");
    }
}

ZTEST(gloveassist, test_xor_corruption_detected)
{
    /* Flip a single wire bit in the obfuscated payload â€” CRC must catch it */
    glove_frame_t frame;
    frame_build(&frame, MSG_TYPE_GESTURE, NULL, 0U);
    frame.payload[0] ^= 0xFFU;  /* corrupt one wire byte */

    int32_t ret = frame_validate(&frame);
    zassert_not_equal(ret, 0,
                      "Wire corruption must be detected by CRC");
}

/* ------------------------------------------------------------------ */
/*  Payload struct sizing checks                           FR-007      */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_sensor_payload_size_fits_frame)
{
    zassert_equal(sizeof(frame_sensor_payload_t), (size_t)PROTO_PAYLOAD_SIZE,
                  "frame_sensor_payload_t size %u != PROTO_PAYLOAD_SIZE %u",
                  sizeof(frame_sensor_payload_t), PROTO_PAYLOAD_SIZE);
}

ZTEST(gloveassist, test_heartbeat_payload_size_fits_frame)
{
    zassert_equal(sizeof(frame_heartbeat_payload_t), (size_t)PROTO_PAYLOAD_SIZE,
                  "frame_heartbeat_payload_t size %u != PROTO_PAYLOAD_SIZE %u",
                  sizeof(frame_heartbeat_payload_t), PROTO_PAYLOAD_SIZE);
}

/* ------------------------------------------------------------------ */
/*  ADC & protocol constants sanity checks                            */
/* ------------------------------------------------------------------ */

ZTEST(gloveassist, test_adc_min_less_than_max)
{
    zassert_true((uint32_t)ADC_MIN_VALID < (uint32_t)ADC_MAX_VALID,
                 "ADC_MIN_VALID (%u) must be < ADC_MAX_VALID (%u)",
                 ADC_MIN_VALID, ADC_MAX_VALID);
}

ZTEST(gloveassist, test_adc_thresholds_within_12bit)
{
    zassert_true((uint32_t)ADC_MAX_VALID <= 4095U,
                 "ADC_MAX_VALID (%u) exceeds 12-bit range", ADC_MAX_VALID);
}

ZTEST(gloveassist, test_gesture_thresholds_consistent)
{
    /* Senzor flex: valoarea ADC SCADE la indoire → BENT < OPEN (fizic corect) */
    zassert_true((uint32_t)GESTURE_THRESHOLD_BENT <
                 (uint32_t)GESTURE_THRESHOLD_OPEN,
                 "THRESHOLD_BENT (%u) must be < THRESHOLD_OPEN (%u)",
                 GESTURE_THRESHOLD_BENT, GESTURE_THRESHOLD_OPEN);
}

ZTEST(gloveassist, test_heartbeat_timeout_greater_than_interval)
{
    zassert_true((uint32_t)HEARTBEAT_TIMEOUT_MS >
                 (uint32_t)HEARTBEAT_INTERVAL_MS,
                 "Timeout (%u ms) must exceed interval (%u ms)",
                 HEARTBEAT_TIMEOUT_MS, HEARTBEAT_INTERVAL_MS);
}

ZTEST(gloveassist, test_proto_frame_size_is_12)
{
    /* SOF(1)+TYPE(1)+SEQ(1)+PAYLOAD(8)+CRC8(1) = 12 */
    zassert_equal((uint32_t)PROTO_FRAME_SIZE, 12U,
                  "PROTO_FRAME_SIZE must be 12, got %u", PROTO_FRAME_SIZE);
}

/* ------------------------------------------------------------------ */
/*  Test suite registration                                            */
/* ------------------------------------------------------------------ */
ZTEST_SUITE(gloveassist, NULL, NULL, NULL, NULL, NULL);
