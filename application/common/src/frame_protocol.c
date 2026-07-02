/*
 * GloveAssist Frame Protocol - Implementation
 */

#include "frame_protocol.h"
#include <string.h>

static const uint8_t s_aes_key[FRAME_AES_KEY_LEN] = FRAME_AES_KEY;
static const uint8_t s_nonce_prefix[] = FRAME_AES_NONCE_PREFIX;
static uint32_t s_session_nonce;
static uint8_t s_session_ready;
static uint32_t s_tx_counter;
static uint32_t s_last_rx_counter;
static uint8_t s_rx_counter_seen;

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

uint32_t frame_get_counter(const frame_t *f)
{
    if (f == NULL) {
        return 0U;
    }
    return ((uint32_t)f->counter[0])
         | ((uint32_t)f->counter[1] << 8U)
         | ((uint32_t)f->counter[2] << 16U)
         | ((uint32_t)f->counter[3] << 24U);
}

void frame_secure_reset_session(void)
{
    s_session_nonce   = 0U;
    s_session_ready   = 0U;
    s_tx_counter      = 0U;
    s_last_rx_counter = 0U;
    s_rx_counter_seen = 0U;
}

void frame_secure_set_session(uint32_t session_nonce)
{
    if (session_nonce == 0U) {
        frame_secure_reset_session();
        return;
    }

    s_session_nonce   = session_nonce;
    s_session_ready   = 1U;
    s_tx_counter      = 0U;
    s_last_rx_counter = 0U;
    s_rx_counter_seen = 0U;
}

bool frame_secure_session_ready(void)
{
    return (s_session_ready != 0U);
}

int frame_build(frame_t *out, uint8_t type, uint8_t seq,
                const uint8_t *payload)
{
    if ((out == NULL) || (payload == NULL)) {
        return -1;
    }

    out->sof  = FRAME_SOF;
    out->type = type;
    out->seq  = seq;
    (void)memset(out->counter, 0, FRAME_COUNTER_LEN);
    (void)memcpy(out->payload, payload, FRAME_PAYLOAD_LEN);

    return 0;
}

int frame_validate(frame_t *f)
{
    if (f == NULL) {
        return -1;
    }
    if (f->sof != FRAME_SOF) {
        return -1;
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

    /* Fast recovery after a bad fixed-size window. */
    if (p->buffer[FRAME_SIZE - 1U] == FRAME_SOF) {
        p->active = 1U;
        p->index = 1U;
        p->buffer[0] = FRAME_SOF;
    }

    return -2;
}

void frame_hmac_parser_init(frame_hmac_parser_t *p)
{
    if (p == NULL) {
        return;
    }
    p->index  = 0U;
    p->active = 0U;
    (void)memset(p->buffer, 0, FRAME_HMAC_SIZE);
}

int frame_hmac_parser_push_byte(frame_hmac_parser_t *p, uint8_t byte,
                                frame_hmac_t *out)
{
    if ((p == NULL) || (out == NULL)) {
        return -1;
    }

    if (p->active == 0U) {
        if (byte == FRAME_SOF) {
            p->active = 1U;
            p->index  = 0U;
            p->buffer[p->index++] = byte;
        }
        return 0;
    }

    if (p->index >= FRAME_HMAC_SIZE) {
        /* Defensive reset. */
        p->active = 0U;
        p->index  = 0U;
        if (byte == FRAME_SOF) {
            p->active = 1U;
            p->buffer[p->index++] = byte;
        }
        return 0;
    }

    p->buffer[p->index++] = byte;

    if (p->index < FRAME_HMAC_SIZE) {
        return 0;
    }

    (void)memcpy(out, p->buffer, FRAME_HMAC_SIZE);
    p->active = 0U;
    p->index  = 0U;

    if (frame_validate_hmac(out) == 0) {
        return 1;
    }

    /* Fast recovery: if the last consumed byte is SOF, start new frame. */
    if (p->buffer[FRAME_HMAC_SIZE - 1U] == FRAME_SOF) {
        p->active = 1U;
        p->index  = 1U;
        p->buffer[0] = FRAME_SOF;
    }

    return -2;
}

/* ==========================================================================
 * Portable SHA-256 (FIPS 180-4) - no external dependencies.
 * Tested against NIST test vectors.
 * ========================================================================== */

#define SHA256_BLOCK_LEN  64U
#define SHA256_HASH_LEN   32U

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buf[SHA256_BLOCK_LEN];
    uint8_t  buf_len;
} sha256_ctx_t;

static const uint32_t K256[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

#define ROTR32(x, n)  (((uint32_t)(x) >> (n)) | ((uint32_t)(x) << (32U - (n))))
#define SHA_S0(x)     (ROTR32(x,  2U) ^ ROTR32(x, 13U) ^ ROTR32(x, 22U))
#define SHA_S1(x)     (ROTR32(x,  6U) ^ ROTR32(x, 11U) ^ ROTR32(x, 25U))
#define SHA_s0(x)     (ROTR32(x,  7U) ^ ROTR32(x, 18U) ^ ((uint32_t)(x) >>  3U))
#define SHA_s1(x)     (ROTR32(x, 17U) ^ ROTR32(x, 19U) ^ ((uint32_t)(x) >> 10U))
#define SHA_CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA_MAJ(x,y,z)(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t block[SHA256_BLOCK_LEN])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t t1, t2;
    uint8_t i;

    for (i = 0U; i < 16U; i++) {
        w[i] = ((uint32_t)block[(uint8_t)(i * 4U)]      << 24U)
             | ((uint32_t)block[(uint8_t)(i * 4U + 1U)] << 16U)
             | ((uint32_t)block[(uint8_t)(i * 4U + 2U)] <<  8U)
             | ((uint32_t)block[(uint8_t)(i * 4U + 3U)]);
    }
    for (i = 16U; i < 64U; i++) {
        w[i] = SHA_s1(w[i - 2U]) + w[i - 7U] + SHA_s0(w[i - 15U]) + w[i - 16U];
    }

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0U; i < 64U; i++) {
        t1 = h + SHA_S1(e) + SHA_CH(e, f, g) + K256[i] + w[i];
        t2 = SHA_S0(a) + SHA_MAJ(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6a09e667UL; ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL; ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL; ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL; ctx->state[7] = 0x5be0cd19UL;
    ctx->bit_count = 0U;
    ctx->buf_len   = 0U;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0U; i < len; i++) {
        ctx->buf[ctx->buf_len++] = data[i];
        if (ctx->buf_len == SHA256_BLOCK_LEN) {
            sha256_transform(ctx, ctx->buf);
            ctx->buf_len = 0U;
        }
    }
    ctx->bit_count += (uint64_t)len * 8U;
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t digest[SHA256_HASH_LEN])
{
    uint8_t j;
    uint64_t bc = ctx->bit_count;

    ctx->buf[ctx->buf_len++] = 0x80U;

    if (ctx->buf_len > (SHA256_BLOCK_LEN - 8U)) {
        while (ctx->buf_len < SHA256_BLOCK_LEN) {
            ctx->buf[ctx->buf_len++] = 0x00U;
        }
        sha256_transform(ctx, ctx->buf);
        ctx->buf_len = 0U;
    }
    while (ctx->buf_len < (SHA256_BLOCK_LEN - 8U)) {
        ctx->buf[ctx->buf_len++] = 0x00U;
    }
    for (j = 0U; j < 8U; j++) {
        ctx->buf[(uint8_t)(SHA256_BLOCK_LEN - 8U) + j] =
            (uint8_t)(bc >> (56U - (uint8_t)(j * 8U)));
    }
    sha256_transform(ctx, ctx->buf);

    for (j = 0U; j < 8U; j++) {
        digest[(uint8_t)(j * 4U)]      = (uint8_t)(ctx->state[j] >> 24U);
        digest[(uint8_t)(j * 4U + 1U)] = (uint8_t)(ctx->state[j] >> 16U);
        digest[(uint8_t)(j * 4U + 2U)] = (uint8_t)(ctx->state[j] >>  8U);
        digest[(uint8_t)(j * 4U + 3U)] = (uint8_t)(ctx->state[j]);
    }
}

/* ==========================================================================
 * HMAC-SHA256 (RFC 2104) - internal helper.
 * key_len must be <= SHA256_BLOCK_LEN (64).  Our key is always 32 bytes.
 * ========================================================================== */
#define HMAC_IPAD  0x36U
#define HMAC_OPAD  0x5CU

static void hmac_sha256(const uint8_t *key, uint8_t key_len,
                        const uint8_t *msg, uint8_t msg_len,
                        uint8_t out[SHA256_HASH_LEN])
{
    sha256_ctx_t ctx;
    uint8_t k_ipad[SHA256_BLOCK_LEN];
    uint8_t k_opad[SHA256_BLOCK_LEN];
    uint8_t inner[SHA256_HASH_LEN];
    uint8_t i;

    (void)memset(k_ipad, 0, SHA256_BLOCK_LEN);
    (void)memset(k_opad, 0, SHA256_BLOCK_LEN);
    (void)memcpy(k_ipad, key, key_len);
    (void)memcpy(k_opad, key, key_len);

    for (i = 0U; i < SHA256_BLOCK_LEN; i++) {
        k_ipad[i] ^= HMAC_IPAD;
        k_opad[i] ^= HMAC_OPAD;
    }

    /* Inner hash: SHA256(k_ipad || msg) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, SHA256_BLOCK_LEN);
    sha256_update(&ctx, msg, msg_len);
    sha256_final(&ctx, inner);

    /* Outer hash: SHA256(k_opad || inner) */
    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, SHA256_BLOCK_LEN);
    sha256_update(&ctx, inner, SHA256_HASH_LEN);
    sha256_final(&ctx, out);
}

/* ==========================================================================
 * AES-128 encrypt + CTR mode.  Only encryption is needed for CTR.
 * ========================================================================== */

#define AES_BLOCK_LEN 16U
#define AES_ROUNDS    10U
#define AES_KEY_SCHEDULE_LEN 176U

static const uint8_t AES_SBOX[256] = {
    0x63U, 0x7CU, 0x77U, 0x7BU, 0xF2U, 0x6BU, 0x6FU, 0xC5U,
    0x30U, 0x01U, 0x67U, 0x2BU, 0xFEU, 0xD7U, 0xABU, 0x76U,
    0xCAU, 0x82U, 0xC9U, 0x7DU, 0xFAU, 0x59U, 0x47U, 0xF0U,
    0xADU, 0xD4U, 0xA2U, 0xAFU, 0x9CU, 0xA4U, 0x72U, 0xC0U,
    0xB7U, 0xFDU, 0x93U, 0x26U, 0x36U, 0x3FU, 0xF7U, 0xCCU,
    0x34U, 0xA5U, 0xE5U, 0xF1U, 0x71U, 0xD8U, 0x31U, 0x15U,
    0x04U, 0xC7U, 0x23U, 0xC3U, 0x18U, 0x96U, 0x05U, 0x9AU,
    0x07U, 0x12U, 0x80U, 0xE2U, 0xEBU, 0x27U, 0xB2U, 0x75U,
    0x09U, 0x83U, 0x2CU, 0x1AU, 0x1BU, 0x6EU, 0x5AU, 0xA0U,
    0x52U, 0x3BU, 0xD6U, 0xB3U, 0x29U, 0xE3U, 0x2FU, 0x84U,
    0x53U, 0xD1U, 0x00U, 0xEDU, 0x20U, 0xFCU, 0xB1U, 0x5BU,
    0x6AU, 0xCBU, 0xBEU, 0x39U, 0x4AU, 0x4CU, 0x58U, 0xCFU,
    0xD0U, 0xEFU, 0xAAU, 0xFBU, 0x43U, 0x4DU, 0x33U, 0x85U,
    0x45U, 0xF9U, 0x02U, 0x7FU, 0x50U, 0x3CU, 0x9FU, 0xA8U,
    0x51U, 0xA3U, 0x40U, 0x8FU, 0x92U, 0x9DU, 0x38U, 0xF5U,
    0xBCU, 0xB6U, 0xDAU, 0x21U, 0x10U, 0xFFU, 0xF3U, 0xD2U,
    0xCDU, 0x0CU, 0x13U, 0xECU, 0x5FU, 0x97U, 0x44U, 0x17U,
    0xC4U, 0xA7U, 0x7EU, 0x3DU, 0x64U, 0x5DU, 0x19U, 0x73U,
    0x60U, 0x81U, 0x4FU, 0xDCU, 0x22U, 0x2AU, 0x90U, 0x88U,
    0x46U, 0xEEU, 0xB8U, 0x14U, 0xDEU, 0x5EU, 0x0BU, 0xDBU,
    0xE0U, 0x32U, 0x3AU, 0x0AU, 0x49U, 0x06U, 0x24U, 0x5CU,
    0xC2U, 0xD3U, 0xACU, 0x62U, 0x91U, 0x95U, 0xE4U, 0x79U,
    0xE7U, 0xC8U, 0x37U, 0x6DU, 0x8DU, 0xD5U, 0x4EU, 0xA9U,
    0x6CU, 0x56U, 0xF4U, 0xEAU, 0x65U, 0x7AU, 0xAEU, 0x08U,
    0xBAU, 0x78U, 0x25U, 0x2EU, 0x1CU, 0xA6U, 0xB4U, 0xC6U,
    0xE8U, 0xDDU, 0x74U, 0x1FU, 0x4BU, 0xBDU, 0x8BU, 0x8AU,
    0x70U, 0x3EU, 0xB5U, 0x66U, 0x48U, 0x03U, 0xF6U, 0x0EU,
    0x61U, 0x35U, 0x57U, 0xB9U, 0x86U, 0xC1U, 0x1DU, 0x9EU,
    0xE1U, 0xF8U, 0x98U, 0x11U, 0x69U, 0xD9U, 0x8EU, 0x94U,
    0x9BU, 0x1EU, 0x87U, 0xE9U, 0xCEU, 0x55U, 0x28U, 0xDFU,
    0x8CU, 0xA1U, 0x89U, 0x0DU, 0xBFU, 0xE6U, 0x42U, 0x68U,
    0x41U, 0x99U, 0x2DU, 0x0FU, 0xB0U, 0x54U, 0xBBU, 0x16U
};

static const uint8_t AES_RCON[10] = {
    0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U, 0x1BU, 0x36U
};

static uint8_t aes_xtime(uint8_t x)
{
    return (uint8_t)((x << 1U) ^ (((x >> 7U) & 1U) * 0x1BU));
}

static void aes_key_expand(const uint8_t key[AES_BLOCK_LEN],
                           uint8_t round_key[AES_KEY_SCHEDULE_LEN])
{
    uint8_t temp[4];
    uint8_t bytes = AES_BLOCK_LEN;
    uint8_t rcon = 0U;
    uint8_t i;

    (void)memcpy(round_key, key, AES_BLOCK_LEN);

    while (bytes < AES_KEY_SCHEDULE_LEN) {
        for (i = 0U; i < 4U; i++) {
            temp[i] = round_key[(uint8_t)(bytes - 4U + i)];
        }

        if ((bytes % AES_BLOCK_LEN) == 0U) {
            uint8_t t = temp[0];
            temp[0] = AES_SBOX[temp[1]] ^ AES_RCON[rcon++];
            temp[1] = AES_SBOX[temp[2]];
            temp[2] = AES_SBOX[temp[3]];
            temp[3] = AES_SBOX[t];
        }

        for (i = 0U; i < 4U; i++) {
            round_key[bytes] = round_key[(uint8_t)(bytes - AES_BLOCK_LEN)] ^ temp[i];
            bytes++;
        }
    }
}

static void aes_add_round_key(uint8_t state[AES_BLOCK_LEN], const uint8_t *round_key)
{
    for (uint8_t i = 0U; i < AES_BLOCK_LEN; i++) {
        state[i] ^= round_key[i];
    }
}

static void aes_sub_bytes(uint8_t state[AES_BLOCK_LEN])
{
    for (uint8_t i = 0U; i < AES_BLOCK_LEN; i++) {
        state[i] = AES_SBOX[state[i]];
    }
}

static void aes_shift_rows(uint8_t state[AES_BLOCK_LEN])
{
    uint8_t t;

    t = state[1];  state[1]  = state[5];  state[5]  = state[9];
    state[9] = state[13];    state[13] = t;

    t = state[2];  state[2]  = state[10]; state[10] = t;
    t = state[6];  state[6]  = state[14]; state[14] = t;

    t = state[15]; state[15] = state[11]; state[11] = state[7];
    state[7] = state[3];     state[3] = t;
}

static void aes_mix_columns(uint8_t state[AES_BLOCK_LEN])
{
    for (uint8_t i = 0U; i < AES_BLOCK_LEN; i = (uint8_t)(i + 4U)) {
        uint8_t a0 = state[i];
        uint8_t a1 = state[(uint8_t)(i + 1U)];
        uint8_t a2 = state[(uint8_t)(i + 2U)];
        uint8_t a3 = state[(uint8_t)(i + 3U)];
        uint8_t t = a0 ^ a1 ^ a2 ^ a3;
        uint8_t u = a0;

        state[i]               ^= t ^ aes_xtime((uint8_t)(a0 ^ a1));
        state[(uint8_t)(i + 1U)] ^= t ^ aes_xtime((uint8_t)(a1 ^ a2));
        state[(uint8_t)(i + 2U)] ^= t ^ aes_xtime((uint8_t)(a2 ^ a3));
        state[(uint8_t)(i + 3U)] ^= t ^ aes_xtime((uint8_t)(a3 ^ u));
    }
}

static void aes128_encrypt_block(const uint8_t input[AES_BLOCK_LEN],
                                 uint8_t output[AES_BLOCK_LEN])
{
    uint8_t state[AES_BLOCK_LEN];
    uint8_t round_key[AES_KEY_SCHEDULE_LEN];

    aes_key_expand(s_aes_key, round_key);
    (void)memcpy(state, input, AES_BLOCK_LEN);

    aes_add_round_key(state, round_key);

    for (uint8_t round = 1U; round < AES_ROUNDS; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, &round_key[(uint8_t)(round * AES_BLOCK_LEN)]);
    }

    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, &round_key[(uint8_t)(AES_ROUNDS * AES_BLOCK_LEN)]);

    (void)memcpy(output, state, AES_BLOCK_LEN);
}

static void put_u32_le(uint8_t out[FRAME_COUNTER_LEN], uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFFU);
    out[1] = (uint8_t)((v >> 8U) & 0xFFU);
    out[2] = (uint8_t)((v >> 16U) & 0xFFU);
    out[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static uint32_t get_u32_le(const uint8_t in[FRAME_COUNTER_LEN])
{
    return ((uint32_t)in[0])
         | ((uint32_t)in[1] << 8U)
         | ((uint32_t)in[2] << 16U)
         | ((uint32_t)in[3] << 24U);
}

static uint32_t next_tx_counter(void)
{
    s_tx_counter++;
    if (s_tx_counter == 0U) {
        s_tx_counter = 1U;
    }
    return s_tx_counter;
}

static void make_ctr_block(uint8_t block[AES_BLOCK_LEN],
                           uint8_t role_id,
                           uint32_t counter)
{
    (void)memset(block, 0, AES_BLOCK_LEN);
    (void)memcpy(block, s_nonce_prefix, sizeof(s_nonce_prefix));
    block[7] = (uint8_t)((s_session_nonce >> 24U) & 0xFFU);
    block[8] = (uint8_t)((s_session_nonce >> 16U) & 0xFFU);
    block[9] = (uint8_t)((s_session_nonce >> 8U) & 0xFFU);
    block[10] = (uint8_t)(s_session_nonce & 0xFFU);
    block[11] = role_id;
    block[12] = (uint8_t)((counter >> 24U) & 0xFFU);
    block[13] = (uint8_t)((counter >> 16U) & 0xFFU);
    block[14] = (uint8_t)((counter >> 8U) & 0xFFU);
    block[15] = (uint8_t)(counter & 0xFFU);
}

static void aes_ctr_payload_crypt(uint8_t role_id,
                                  uint32_t counter,
                                  uint8_t payload[FRAME_PAYLOAD_LEN])
{
    uint8_t ctr_block[AES_BLOCK_LEN];
    uint8_t stream[AES_BLOCK_LEN];

    make_ctr_block(ctr_block, role_id, counter);
    aes128_encrypt_block(ctr_block, stream);

    for (uint8_t i = 0U; i < FRAME_PAYLOAD_LEN; i++) {
        payload[i] ^= stream[i];
    }
}

/* Shared HMAC key - must match on both STM32 and ESP32. */
static const uint8_t s_hmac_key[FRAME_HMAC_KEY_LEN] = FRAME_HMAC_KEY;

int frame_build_session(frame_hmac_t *out, uint8_t seq,
                        uint8_t msg, uint32_t session_nonce)
{
    uint8_t payload[FRAME_PAYLOAD_LEN] = {0U};
    uint8_t digest[SHA256_HASH_LEN];

    if ((out == NULL) || (session_nonce == 0U)) {
        return -1;
    }

    payload[0] = msg;
    if (frame_build(&out->base, (uint8_t)FRAME_TYPE_SESSION, seq, payload) != 0) {
        return -1;
    }
    put_u32_le(out->base.counter, session_nonce);

    hmac_sha256(s_hmac_key, (uint8_t)sizeof(s_hmac_key),
                (const uint8_t *)&out->base, (uint8_t)FRAME_SIZE,
                digest);
    (void)memcpy(out->tag, digest, FRAME_HMAC_TAG_LEN);

    return 0;
}

int frame_build_hmac(frame_hmac_t *out, uint8_t type, uint8_t seq,
                     const uint8_t *payload)
{
    uint8_t digest[SHA256_HASH_LEN];
    uint32_t counter;

    if ((out == NULL) || (payload == NULL)) {
        return -1;
    }
    if (type == (uint8_t)FRAME_TYPE_SESSION) {
        return -1;
    }
    if (s_session_ready == 0U) {
        return -1;
    }

    if (frame_build(&out->base, type, seq, payload) != 0) {
        return -1;
    }

    counter = next_tx_counter();
    put_u32_le(out->base.counter, counter);

    aes_ctr_payload_crypt((uint8_t)FRAME_SECURE_TX_ID, counter, out->base.payload);

    hmac_sha256(s_hmac_key, (uint8_t)sizeof(s_hmac_key),
                (const uint8_t *)&out->base, (uint8_t)FRAME_SIZE,
                digest);

    (void)memcpy(out->tag, digest, FRAME_HMAC_TAG_LEN);

    return 0;
}

int frame_validate_hmac(frame_hmac_t *f)
{
    uint8_t digest[SHA256_HASH_LEN];
    uint8_t diff = 0U;
    uint8_t i;
    uint32_t counter;

    if (f == NULL) {
        return -1;
    }
    if (f->base.sof != FRAME_SOF) {
        return -1;
    }

    hmac_sha256(s_hmac_key, (uint8_t)sizeof(s_hmac_key),
                (const uint8_t *)&f->base, (uint8_t)FRAME_SIZE,
                digest);

    for (i = 0U; i < FRAME_HMAC_TAG_LEN; i++) {
        diff |= digest[i] ^ f->tag[i];
    }

    if (diff != 0U) {
        return -1;
    }
    if (f->base.type == (uint8_t)FRAME_TYPE_SESSION) {
        if (get_u32_le(f->base.counter) == 0U) {
            return -1;
        }
        return frame_validate(&f->base);
    }
    if (s_session_ready == 0U) {
        return -1;
    }

    counter = get_u32_le(f->base.counter);
    if (counter == 0U) {
        return -1;
    }

#if FRAME_SECURE_ANTI_REPLAY
    if ((s_rx_counter_seen != 0U) && (counter <= s_last_rx_counter)) {
        return -1;
    }
#endif

    aes_ctr_payload_crypt((uint8_t)FRAME_SECURE_RX_ID, counter, f->base.payload);

#if FRAME_SECURE_ANTI_REPLAY
    s_last_rx_counter = counter;
    s_rx_counter_seen = 1U;
#endif

    return frame_validate(&f->base);
}
