/**
 * @file security.c
 * @brief BLE payload obfuscation, honey-pot generation, anti-replay
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "frame_protocol.h"
#include "error_codes.h"
#include "security.h"

LOG_MODULE_REGISTER(security, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Dynamic XOR key ---------- */
static uint8_t  xor_key[PROTO_PAYLOAD_SIZE];
static int64_t key_rotation_ts;

static void rotate_xor_key(void)
{
    sys_rand_get(xor_key, sizeof(xor_key));
    key_rotation_ts = k_uptime_get();
}

void security_xor_payload(uint8_t *payload, uint8_t len)
{
    if ((k_uptime_get() - key_rotation_ts) >
        (int64_t)(KEY_ROTATION_SEC * 1000)) {
        rotate_xor_key();
    }

    for (uint8_t i = 0U; i < len; i++) {
        payload[i] ^= xor_key[i % sizeof(xor_key)];
    }
}

/* ---------- Honey-pot packet generation ---------- */
int security_gen_honeypot(glove_frame_t *frame)
{
    uint8_t fake_payload[PROTO_PAYLOAD_SIZE];
    sys_rand_get(fake_payload, sizeof(fake_payload));
    return (int)frame_build(frame, MSG_TYPE_HONEYPOT,
                                fake_payload, (uint8_t)PROTO_PAYLOAD_SIZE);
}

/* ---------- Anti-replay / lockdown ---------- */
static u8_t consecutive_invalid;
static bool lockdown_active;
static bool seq_initialized;
static uint8_t last_seq;

int security_check_frame(const glove_frame_t *frame)
{
    if (lockdown_active) {
        LOG_ERR("Lockdown active — rejecting all frames");
        return (int)ERR_SECURITY_LOCKDOWN;
    }

    if (frame->type == MSG_TYPE_HONEYPOT) {
        return 0;
    }

    int ret = frame_validate(frame);
    if (ret != 0) {
        consecutive_invalid++;
        LOG_WRN("Invalid frame #%u/%u", consecutive_invalid,
                LOCKDOWN_INVALID_FRAMES);

        if (consecutive_invalid >= LOCKDOWN_INVALID_FRAMES) {
            lockdown_active = true;
            LOG_ERR("** LOCKDOWN TRIGGERED — %u consecutive invalid frames",
                    LOCKDOWN_INVALID_FRAMES);
            return (int)ERR_SECURITY_LOCKDOWN;
        }
        return ret;
    }

    if (!seq_initialized) {
        seq_initialized = true;
        last_seq = frame->seq;
    } else {
        uint8_t delta = (uint8_t)(frame->seq - last_seq);
        if ((delta == 0U) || (delta > (uint8_t)COUNTER_MAX_GAP)) {
            consecutive_invalid++;
            LOG_WRN("Replay/counter anomaly delta=%u #%u/%u",
                    delta, consecutive_invalid, LOCKDOWN_INVALID_FRAMES);

            if (consecutive_invalid >= LOCKDOWN_INVALID_FRAMES) {
                lockdown_active = true;
                LOG_ERR("** LOCKDOWN TRIGGERED — counter anomaly storm");
                return (int)ERR_SECURITY_LOCKDOWN;
            }
            return (int)ERR_SECURITY_REPLAY;
        }
        last_seq = frame->seq;
    }

    consecutive_invalid = 0U;
    return 0;
}

int security_init(void)
{
    lockdown_active     = false;
    consecutive_invalid = 0U;
    seq_initialized     = false;
    last_seq            = 0U;
    rotate_xor_key();

    LOG_INF("Security subsystem initialised (honeypot=%u:1, keyrot=%us)",
            HONEYPOT_RATIO, KEY_ROTATION_SEC);
    return 0;
}
