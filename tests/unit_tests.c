/*
 * Ztest unit tests for the transport-agnostic frame protocol.
 *
 * Build target:
 *   west build -b native_sim/native/64 tests/
 */

#include <zephyr/ztest.h>
#include <string.h>

#include "frame_protocol.h"

static void before_each(void *fixture)
{
    (void)fixture;
    frame_secure_reset_session();
}

ZTEST(gloveassist, test_frame_sizes_match_wire_format)
{
    zassert_equal(sizeof(frame_t), (size_t)FRAME_SIZE,
                  "frame_t size mismatch");
    zassert_equal((uint32_t)FRAME_SIZE, 15U,
                  "base frame must be 15 bytes");

    zassert_equal(sizeof(frame_hmac_t), (size_t)FRAME_HMAC_SIZE,
                  "frame_hmac_t size mismatch");
    zassert_equal((uint32_t)FRAME_HMAC_SIZE, 23U,
                  "secure frame must be 23 bytes");
}

ZTEST(gloveassist, test_crc8_maxim_known_vector)
{
    static const uint8_t data[] = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };

    zassert_equal(frame_crc8(data, (uint8_t)sizeof(data)), 0xA1U,
                  "CRC-8/MAXIM check vector mismatch");
    zassert_equal(frame_crc8(NULL, 9U), 0U,
                  "NULL CRC input should return 0");
}

ZTEST(gloveassist, test_sensor_payload_roundtrip)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];
    uint16_t index;
    uint16_t middle;
    uint16_t ring;
    uint16_t pinky;

    frame_make_sensor_payload(payload, 123U, 1024U, 2048U, 4095U);
    frame_decode_sensor_payload(payload, &index, &middle, &ring, &pinky);

    zassert_equal(index, 123U, "index payload mismatch");
    zassert_equal(middle, 1024U, "middle payload mismatch");
    zassert_equal(ring, 2048U, "ring payload mismatch");
    zassert_equal(pinky, 4095U, "pinky payload mismatch");
}

ZTEST(gloveassist, test_command_and_status_payload_helpers)
{
    uint8_t payload[FRAME_PAYLOAD_LEN];

    frame_make_cmd_payload(payload, (uint8_t)CMD_OLED_TEXT, 7U, "HELLO!");
    zassert_equal(payload[0], (uint8_t)CMD_OLED_TEXT, "command mismatch");
    zassert_equal(payload[1], 7U, "arg0 mismatch");
    zassert_equal(0, memcmp(&payload[2], "HELLO!", 6U),
                  "text payload mismatch");

    frame_make_status_payload(payload, (uint8_t)STATUS_ACK, 42U);
    zassert_equal(payload[0], (uint8_t)STATUS_ACK, "status mismatch");
    zassert_equal(payload[1], 42U, "status seq mismatch");
    for (uint8_t i = 2U; i < FRAME_PAYLOAD_LEN; i++) {
        zassert_equal(payload[i], 0U, "status payload tail must be zero");
    }
}

ZTEST(gloveassist, test_base_frame_build_validate_and_parse)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {
        0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U
    };
    frame_t frame;
    frame_t parsed;
    frame_parser_t parser;
    int ret;

    zassert_equal(frame_build(NULL, (uint8_t)FRAME_TYPE_STATUS, 1U, payload),
                  -1, "NULL frame should fail");
    zassert_equal(frame_build(&frame, (uint8_t)FRAME_TYPE_STATUS, 1U, NULL),
                  -1, "NULL payload should fail");

    ret = frame_build(&frame, (uint8_t)FRAME_TYPE_STATUS, 1U, payload);
    zassert_equal(ret, 0, "base frame build failed");
    zassert_equal(frame.sof, (uint8_t)FRAME_SOF, "SOF mismatch");
    zassert_equal(frame.type, (uint8_t)FRAME_TYPE_STATUS, "type mismatch");
    zassert_equal(frame.seq, 1U, "seq mismatch");
    zassert_equal(frame_get_counter(&frame), 0U, "base counter must be zero");
    zassert_equal(0, memcmp(frame.payload, payload, FRAME_PAYLOAD_LEN),
                  "base payload mismatch");
    zassert_equal(frame_validate(&frame), 0, "base frame validate failed");

    frame_parser_init(&parser);
    zassert_equal(frame_parser_push_byte(&parser, 0x55U, &parsed), 0,
                  "noise before SOF should be ignored");
    const uint8_t *wire = (const uint8_t *)&frame;
    for (uint8_t i = 0U; i < FRAME_SIZE; i++) {
        ret = frame_parser_push_byte(&parser, wire[i], &parsed);
        zassert_equal(ret, (i == (FRAME_SIZE - 1U)) ? 1 : 0,
                      "parser returned unexpected state at byte %u", i);
    }
    zassert_equal(0, memcmp(&parsed, &frame, FRAME_SIZE),
                  "parsed base frame mismatch");
}

ZTEST(gloveassist, test_session_frame_is_hmac_authenticated)
{
    frame_hmac_t session;

    zassert_false(frame_secure_session_ready(),
                  "session should start unavailable");
    zassert_equal(frame_build_session(NULL, 1U, (uint8_t)SESSION_HELLO,
                                      0x12345678UL),
                  -1, "NULL session frame should fail");
    zassert_equal(frame_build_session(&session, 1U, (uint8_t)SESSION_HELLO, 0U),
                  -1, "zero session nonce should fail");

    zassert_equal(frame_build_session(&session, 1U, (uint8_t)SESSION_HELLO,
                                      0x12345678UL),
                  0, "session frame build failed");
    zassert_equal(session.base.type, (uint8_t)FRAME_TYPE_SESSION,
                  "session frame type mismatch");
    zassert_equal(session.base.payload[0], (uint8_t)SESSION_HELLO,
                  "session message mismatch");
    zassert_equal(frame_get_counter(&session.base), 0x12345678UL,
                  "session nonce mismatch");
    zassert_equal(frame_validate_hmac(&session), 0,
                  "session HMAC validation failed");
}

ZTEST(gloveassist, test_hmac_frame_requires_ready_session)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_hmac_t frame;

    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_STATUS,
                                   1U, payload),
                  -1, "secure frame should require negotiated session");

    frame_secure_set_session(0xCAFEBABEU);
    zassert_true(frame_secure_session_ready(), "session should be ready");
    zassert_equal(frame_build_hmac(NULL, (uint8_t)FRAME_TYPE_STATUS,
                                   1U, payload),
                  -1, "NULL secure frame should fail");
    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_SESSION,
                                   1U, payload),
                  -1, "session type must use frame_build_session()");
}

ZTEST(gloveassist, test_hmac_roundtrip_encrypts_and_decrypts_payload)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {
        0xA0U, 0xA1U, 0xA2U, 0xA3U, 0xA4U, 0xA5U, 0xA6U, 0xA7U
    };
    frame_hmac_t frame;

    frame_secure_set_session(0x01020304UL);

    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_COMMAND,
                                   17U, payload),
                  0, "secure frame build failed");
    zassert_equal(frame.base.sof, (uint8_t)FRAME_SOF, "SOF mismatch");
    zassert_equal(frame.base.type, (uint8_t)FRAME_TYPE_COMMAND,
                  "secure frame type mismatch");
    zassert_equal(frame.base.seq, 17U, "secure frame seq mismatch");
    zassert_equal(frame_get_counter(&frame.base), 1U,
                  "first secure frame counter should be 1");
    zassert_not_equal(memcmp(frame.base.payload, payload, FRAME_PAYLOAD_LEN), 0,
                      "wire payload should be encrypted");

    zassert_equal(frame_validate_hmac(&frame), 0,
                  "secure frame validation failed");
    zassert_equal(0, memcmp(frame.base.payload, payload, FRAME_PAYLOAD_LEN),
                  "secure payload was not decrypted correctly");
}

ZTEST(gloveassist, test_hmac_rejects_tag_and_payload_tamper)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U
    };
    frame_hmac_t frame;

    frame_secure_set_session(0x55667788UL);
    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_SENSOR_RAW,
                                   9U, payload),
                  0, "secure frame build failed");
    frame.tag[0] ^= 0x01U;
    zassert_equal(frame_validate_hmac(&frame), -1,
                  "tag tamper should be rejected");

    frame_secure_set_session(0x55667788UL);
    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_SENSOR_RAW,
                                   9U, payload),
                  0, "secure frame build failed");
    frame.base.payload[0] ^= 0x01U;
    zassert_equal(frame_validate_hmac(&frame), -1,
                  "ciphertext tamper should be rejected");
}

ZTEST(gloveassist, test_hmac_replay_is_rejected)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {
        9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U
    };
    frame_hmac_t wire1;
    frame_hmac_t wire2;

    frame_secure_set_session(0x0A0B0C0DUL);
    zassert_equal(frame_build_hmac(&wire1, (uint8_t)FRAME_TYPE_HEARTBEAT,
                                   2U, payload),
                  0, "secure frame build failed");
    wire2 = wire1;

    zassert_equal(frame_validate_hmac(&wire1), 0,
                  "first secure frame should validate");
    zassert_equal(frame_validate_hmac(&wire2), -1,
                  "replayed secure frame should be rejected");
}

ZTEST(gloveassist, test_hmac_streaming_parser)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {
        0x42U, 0x43U, 0x44U, 0x45U, 0x46U, 0x47U, 0x48U, 0x49U
    };
    frame_hmac_t frame;
    frame_hmac_t parsed;
    frame_hmac_parser_t parser;
    int ret;

    frame_secure_set_session(0x11223344UL);
    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_STATUS,
                                   3U, payload),
                  0, "secure frame build failed");

    frame_hmac_parser_init(&parser);
    zassert_equal(frame_hmac_parser_push_byte(&parser, 0x00U, &parsed), 0,
                  "noise before SOF should be ignored");

    const uint8_t *wire = (const uint8_t *)&frame;
    for (uint8_t i = 0U; i < FRAME_HMAC_SIZE; i++) {
        ret = frame_hmac_parser_push_byte(&parser, wire[i], &parsed);
        zassert_equal(ret, (i == (FRAME_HMAC_SIZE - 1U)) ? 1 : 0,
                      "HMAC parser returned unexpected state at byte %u", i);
    }
    zassert_equal(parsed.base.type, (uint8_t)FRAME_TYPE_STATUS,
                  "parsed secure type mismatch");
    zassert_equal(0, memcmp(parsed.base.payload, payload, FRAME_PAYLOAD_LEN),
                  "parsed secure payload mismatch");
}

ZTEST(gloveassist, test_hmac_parser_rejects_bad_frame)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    frame_hmac_t frame;
    frame_hmac_t parsed;
    frame_hmac_parser_t parser;
    int ret = 0;

    frame_secure_set_session(0x99887766UL);
    zassert_equal(frame_build_hmac(&frame, (uint8_t)FRAME_TYPE_STATUS,
                                   4U, payload),
                  0, "secure frame build failed");
    frame.tag[FRAME_HMAC_TAG_LEN - 1U] ^= 0x80U;

    frame_hmac_parser_init(&parser);
    const uint8_t *wire = (const uint8_t *)&frame;
    for (uint8_t i = 0U; i < FRAME_HMAC_SIZE; i++) {
        ret = frame_hmac_parser_push_byte(&parser, wire[i], &parsed);
    }

    zassert_equal(ret, -2, "bad HMAC frame should be rejected by parser");
}

ZTEST_SUITE(gloveassist, NULL, NULL, before_each, NULL, NULL);
