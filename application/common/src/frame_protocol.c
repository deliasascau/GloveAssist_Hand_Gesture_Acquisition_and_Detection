/*
 * GloveAssist Frame Protocol - Implementation
 */

#include "frame_protocol.h"
#include <string.h>

/* Payload whitening key - must match on both STM32 and ESP32. */
static const uint8_t s_whitening_key[FRAME_PAYLOAD_LEN] = FRAME_WHITENING_KEY;

/* CRC-8/MAXIM (Dallas/Maxim, reflected). */
uint8_t frame_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00U;

    if (data == NULL) {
        return 0U;
    }

    for (uint8_t i = 0U; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0U; b < 8U; b++) {
            if ((crc & 0x01U) != 0U) {
                /* Reflected polynomial for 0x31 is 0x8C. */
                crc = (uint8_t)((crc >> 1U) ^ 0x8CU);
            } else {
                crc = (uint8_t)(crc >> 1U);
            }
        }
    }
    return crc;
}

void frame_make_sensor_payload(uint8_t payload[FRAME_PAYLOAD_LEN],
                               uint16_t index,
                               uint16_t middle,
                               uint16_t ring,
                               uint16_t pinky)
{
    if (payload == NULL) {
        return;
    }

    payload[0] = (uint8_t)(index & 0xFFU);
    payload[1] = (uint8_t)((index >> 8U) & 0xFFU);

    payload[2] = (uint8_t)(middle & 0xFFU);
    payload[3] = (uint8_t)((middle >> 8U) & 0xFFU);

    payload[4] = (uint8_t)(ring & 0xFFU);
    payload[5] = (uint8_t)((ring >> 8U) & 0xFFU);

    payload[6] = (uint8_t)(pinky & 0xFFU);
    payload[7] = (uint8_t)((pinky >> 8U) & 0xFFU);
}

void frame_decode_sensor_payload(const uint8_t payload[FRAME_PAYLOAD_LEN],
                                 uint16_t *index,
                                 uint16_t *middle,
                                 uint16_t *ring,
                                 uint16_t *pinky)
{
    if ((payload == NULL) || (index == NULL) || (middle == NULL)
        || (ring == NULL) || (pinky == NULL)) {
        return;
    }

    *index  = (uint16_t)((uint16_t)payload[0] | ((uint16_t)payload[1] << 8U));
    *middle = (uint16_t)((uint16_t)payload[2] | ((uint16_t)payload[3] << 8U));
    *ring   = (uint16_t)((uint16_t)payload[4] | ((uint16_t)payload[5] << 8U));
    *pinky  = (uint16_t)((uint16_t)payload[6] | ((uint16_t)payload[7] << 8U));
}

void frame_make_cmd_payload(uint8_t payload[FRAME_PAYLOAD_LEN],
                            uint8_t cmd,
                            uint8_t arg0,
                            const char *text)
{
    if (payload == NULL) {
        return;
    }

    (void)memset(payload, 0, FRAME_PAYLOAD_LEN);
    payload[0] = cmd;
    payload[1] = arg0;

    if (text != NULL) {
        /* Copy up to 6 ASCII chars into payload[2..7]. */
        for (uint8_t i = 0U; i < 6U; i++) {
            if (text[i] == '\0') {
                break;
            }
            payload[2U + i] = (uint8_t)text[i];
        }
    }
}

void frame_make_status_payload(uint8_t payload[FRAME_PAYLOAD_LEN],
                               uint8_t status,
                               uint8_t seq)
{
    if (payload == NULL) {
        return;
    }

    (void)memset(payload, 0, FRAME_PAYLOAD_LEN);
    payload[0] = status;
    payload[1] = seq;
}

/* Frame builder. */
int frame_build(frame_t *out, uint8_t type, uint8_t seq,
                const uint8_t *payload)
{
    if ((out == NULL) || (payload == NULL)) {
        return -1;
    }

    out->sof  = FRAME_SOF;
    out->type = type;
    out->seq  = seq;

    /* Whiten payload before placing on the wire:
     * wire_byte[i] = plaintext[i] ^ key[i] ^ seq */
    for (uint8_t i = 0U; i < FRAME_PAYLOAD_LEN; i++) {
        out->payload[i] = payload[i] ^ s_whitening_key[i] ^ seq;
    }

    /* CRC covers: type(1) + seq(1) + whitened_payload(8) = 10 bytes */
    out->crc = frame_crc8(&out->type, 10U);

    return 0;
}

/* Frame validator + dewhitener. */
int frame_validate(frame_t *f)
{
    if (f == NULL) {
        return -1;
    }
    if (f->sof != FRAME_SOF) {
        return -1;
    }

    /* CRC was computed over whitened bytes - validate as stored. */
    uint8_t expected = frame_crc8(&f->type, 10U);

    if (f->crc != expected) {
        return -1;
    }

    /* CRC OK: dewhiten payload in-place so caller receives plaintext.
     * wire_byte[i] ^ key[i] ^ seq = plaintext[i]  (XOR is self-inverse). */
    for (uint8_t i = 0U; i < FRAME_PAYLOAD_LEN; i++) {
        f->payload[i] ^= s_whitening_key[i] ^ f->seq;
    }

    return 0;
}

void frame_parser_init(frame_parser_t *p)
{
    if (p == NULL) {
        return;
    }

    p->index = 0U;
    p->active = 0U;
    (void)memset(p->buffer, 0, FRAME_SIZE);
}

int frame_parser_push_byte(frame_parser_t *p, uint8_t byte, frame_t *out)
{
    if ((p == NULL) || (out == NULL)) {
        return -1;
    }

    if (p->active == 0U) {
        if (byte == FRAME_SOF) {
            p->active = 1U;
            p->index = 0U;
            p->buffer[p->index++] = byte;
        }
        return 0;
    }

    if (p->index >= FRAME_SIZE) {
        /* Defensive reset if state is corrupted. Do not drop the current
         * byte; re-check it as a potential SOF for immediate resync. */
        p->active = 0U;
        p->index = 0U;
        if (byte == FRAME_SOF) {
            p->active = 1U;
            p->buffer[p->index++] = byte;
        }
        return 0;
    }

    p->buffer[p->index++] = byte;

    if (p->index < FRAME_SIZE) {
        return 0;
    }

    (void)memcpy(out, p->buffer, FRAME_SIZE);
    p->active = 0U;
    p->index = 0U;

    if (frame_validate(out) == 0) {
        return 1;
    }

    /* Fast recovery after a bad 12-byte window: if the last consumed byte is
     * SOF, treat it as the start of the next candidate frame. This avoids
     * dropping an entire additional frame after a one-byte loss while still
     * allowing 0xAA inside payload of valid frames. */
    if (out->crc == FRAME_SOF) {
        p->active = 1U;
        p->index = 1U;
        p->buffer[0] = FRAME_SOF;
    }

    return -2;
}
