/**
 * @file frame_protocol.c
 * @brief CRC, frame build, validation, and payload decode helpers.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "frame_protocol.h"

static uint8_t s_seq_counter;

uint8_t crc8_ccitt(const uint8_t *data, uint32_t length)
{
    uint8_t crc = 0x00U;

    if (data == NULL) {
        return crc;
    }

    for (uint32_t i = 0U; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((uint8_t)(crc << 1U) ^ 0x07U);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return crc;
}

static void decode_payload(uint8_t seq,
                           const uint8_t in[PROTO_PAYLOAD_SIZE],
                           uint8_t out[PROTO_PAYLOAD_SIZE])
{
    const uint8_t xor_key = (uint8_t)(seq ^ (uint8_t)PROTO_XOR_KEY_BASE);

    for (uint8_t i = 0U; i < (uint8_t)PROTO_PAYLOAD_SIZE; i++) {
        out[i] = (uint8_t)(in[i] ^ xor_key);
    }
}

static uint8_t frame_crc(uint8_t type,
                         uint8_t seq,
                         const uint8_t payload[PROTO_PAYLOAD_SIZE])
{
    uint8_t crc_input[2U + PROTO_PAYLOAD_SIZE];

    crc_input[0] = type;
    crc_input[1] = seq;
    (void)memcpy(&crc_input[2], payload, PROTO_PAYLOAD_SIZE);

    return crc8_ccitt(crc_input, (uint32_t)sizeof(crc_input));
}

int32_t frame_build(glove_frame_t *frame,
                    uint8_t msg_type,
                    const uint8_t *payload,
                    uint8_t payload_len)
{
    uint8_t plain[PROTO_PAYLOAD_SIZE];

    if (frame == NULL) {
        return -EINVAL;
    }
    if (payload_len > (uint8_t)PROTO_PAYLOAD_SIZE) {
        return -EMSGSIZE;
    }
    if ((payload == NULL) && (payload_len > 0U)) {
        return -EINVAL;
    }

    (void)memset(plain, 0, sizeof(plain));
    if (payload_len > 0U) {
        (void)memcpy(plain, payload, payload_len);
    }

    frame->sof = (uint8_t)PROTO_SOF;
    frame->type = msg_type;
    frame->seq = s_seq_counter;
    s_seq_counter++;
    frame->crc8 = frame_crc(frame->type, frame->seq, plain);

    decode_payload(frame->seq, plain, frame->payload);

    return 0;
}

int32_t frame_validate(const glove_frame_t *frame)
{
    uint8_t plain[PROTO_PAYLOAD_SIZE];
    uint8_t expected_crc;

    if (frame == NULL) {
        return -EINVAL;
    }
    if (frame->sof != (uint8_t)PROTO_SOF) {
        return -EBADMSG;
    }

    decode_payload(frame->seq, frame->payload, plain);
    expected_crc = frame_crc(frame->type, frame->seq, plain);

    if (expected_crc != frame->crc8) {
        return -EBADMSG;
    }

    return 0;
}

void frame_decode_payload(const glove_frame_t *frame,
                          uint8_t out[PROTO_PAYLOAD_SIZE])
{
    if ((frame == NULL) || (out == NULL)) {
        return;
    }

    decode_payload(frame->seq, frame->payload, out);
}
