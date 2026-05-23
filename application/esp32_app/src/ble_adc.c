#include "ble_adc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "frame_protocol.h"
#include "gesture.h"

LOG_MODULE_REGISTER(ble_adc, LOG_LEVEL_INF);

/* Last confirmed gesture — written by main thread, read by BLE RX thread.
 * volatile prevents compiler reordering; single-word enum access is atomic on ARM. */
static volatile gesture_id_t s_last_gesture = GESTURE_NONE;

/* BLE command flags — set by rx_write, consumed by main loop. */
static volatile bool        s_calibration_requested;
static volatile bool        s_ota_requested;
static volatile bool        s_caregiver_ack_requested;
static volatile gesture_id_t s_gesture_at_ack;

static struct bt_uuid_128 nus_svc_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

static struct bt_conn *current_conn;
static bool notify_enabled;

static uint8_t cmd_to_upper(uint8_t c)
{
    if ((c >= (uint8_t)'a') && (c <= (uint8_t)'z')) {
        return (uint8_t)(c - ((uint8_t)'a' - (uint8_t)'A'));
    }
    return c;
}

static uint16_t cmd_skip_ws(const uint8_t *data, uint16_t len, uint16_t idx)
{
    while (idx < len) {
        uint8_t c = data[idx];
        if ((c != (uint8_t)' ') && (c != (uint8_t)'\t')
            && (c != (uint8_t)'\r') && (c != (uint8_t)'\n')) {
            break;
        }
        idx++;
    }
    return idx;
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);

    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("BLE notify %s", notify_enabled ? "enabled" : "disabled");
}

static ssize_t rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset,
                        uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    const uint8_t *data = (const uint8_t *)buf;
    uint16_t i0 = cmd_skip_ws(data, len, 0U);
    uint8_t c0 = 0U;
    uint8_t c1 = 0U;
    uint8_t first = (len > 0U) ? data[0] : 0U;

    if (i0 < len) {
        c0 = cmd_to_upper(data[i0]);
        uint16_t i1 = cmd_skip_ws(data, len, (uint16_t)(i0 + 1U));
        if (i1 < len) {
            c1 = cmd_to_upper(data[i1]);
        }
    }

    LOG_INF("BLE RX: %u bytes  first=0x%02x cmd=%c",
            len, first, (c0 != 0U) ? (char)c0 : '-');

    /* Accept:
     *   - single digit '1'..'9' (0x31..0x39) sent from any BLE app
     *   - legacy "OK" / "ok" string for backward compat
     * All trigger caregiver ACK forwarded to STM32 via UART.
     */
    bool is_digit_ack = (c0 >= (uint8_t)'1') && (c0 <= (uint8_t)'9');
    bool is_ok_ack    = (c0 == (uint8_t)'O') && (c1 == (uint8_t)'K');
    bool is_calibrate = (c0 == (uint8_t)'C');
    bool is_ota       = (c0 == (uint8_t)'U');

    if (is_digit_ack || is_ok_ack) {
        /* Queue for main loop — main will send CMD_CAREGIVER_ACK to STM32
         * and BLE "Understood" notification back to phone. */
        s_gesture_at_ack          = s_last_gesture;
        s_caregiver_ack_requested = true;
        LOG_INF("Caregiver ACK queued (gesture %u)", (unsigned)s_last_gesture);
    }

    if (is_calibrate) {
        s_calibration_requested = true;
        LOG_INF("BLE: calibration requested");
    }

    if (is_ota) {
        s_ota_requested = true;
        LOG_INF("BLE: OTA reboot requested");
    }

    return (ssize_t)len;
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, "GloveAssist", sizeof("GloveAssist") - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)),
};

BT_GATT_SERVICE_DEFINE(nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_svc_uuid),
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, rx_write, NULL),
);

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0U) {
        LOG_ERR("BLE connection failed: 0x%02x", err);
        return;
    }

    current_conn = bt_conn_ref(conn);

    /* Request encrypted link immediately after connection.
     * BT_SECURITY_L2 = unauthenticated pairing (Just-Works) with encryption.
     * Prevents passive BLE sniffing without requiring a display/passkey. */
    int sec_err = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (sec_err != 0) {
        LOG_WRN("BLE security request failed: %d", sec_err);
    }

    LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);

    LOG_INF("BLE disconnected: 0x%02x", reason);
    if (current_conn != NULL) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    notify_enabled = false;

    /* Restart advertising automatically so phone can reconnect
     * without needing to reset the ESP32. */
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));
    if (err != 0) {
        LOG_WRN("BLE re-adv failed: %d", err);
    } else {
        LOG_INF("BLE advertising restarted");
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    ARG_UNUSED(conn);

    if (err != BT_SECURITY_ERR_SUCCESS) {
        LOG_WRN("BLE security failed (level %d, err %d) - link not encrypted",
                (int)level, (int)err);
        return;
    }
    LOG_INF("BLE link encrypted (security level %d)", (int)level);
}

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected       = connected,
    .disconnected    = disconnected,
    .security_changed = security_changed,
};

int ble_adc_init(void)
{
    int err = bt_enable(NULL);

    if (err != 0) {
        LOG_ERR("bt_enable failed: %d", err);
        return err;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
    if (err != 0) {
        LOG_ERR("BLE advertising failed: %d", err);
        return err;
    }

    LOG_INF("BLE advertising as GloveAssist");
    return 0;
}

int ble_adc_send_values(const uint16_t adc[4])
{
    char buf[20];
    int  len;

    if ((current_conn == NULL) || !notify_enabled) {
        return -ENOTCONN;
    }

    /* Scale 12-bit ADC (0-4095) to 8-bit (0-255) by >> 4.
     * Format "A:aaa bbb ccc ddd\n" fits in 20-byte ATT MTU:
     * worst case "A:255 255 255 255\n" = 18 bytes. */
    len = snprintf(buf, sizeof(buf), "A:%u %u %u %u\n",
                   (unsigned)(adc[0] >> 4U),
                   (unsigned)(adc[1] >> 4U),
                   (unsigned)(adc[2] >> 4U),
                   (unsigned)(adc[3] >> 4U));
    if ((len <= 0) || (len >= (int)sizeof(buf))) {
        return -EINVAL;
    }

    return bt_gatt_notify(current_conn, &nus_svc.attrs[2], buf, (uint16_t)len);
}

void ble_adc_set_last_gesture(gesture_id_t g)
{
    s_last_gesture = g;
}

bool ble_adc_pop_calibration_request(void)
{
    if (s_calibration_requested) {
        s_calibration_requested = false;
        return true;
    }
    return false;
}

bool ble_adc_pop_ota_request(void)
{
    if (s_ota_requested) {
        s_ota_requested = false;
        return true;
    }
    return false;
}

gesture_id_t ble_adc_pop_caregiver_ack_request(void)
{
    if (s_caregiver_ack_requested) {
        s_caregiver_ack_requested = false;
        return s_gesture_at_ack;
    }
    return GESTURE_NONE;
}

int ble_adc_send_text(const char *text)
{
    if ((current_conn == NULL) || !notify_enabled || (text == NULL)) {
        return -ENOTCONN;
    }
    size_t len = strlen(text);
    if (len > 20U) {
        len = 20U;
    }
    return bt_gatt_notify(current_conn, &nus_svc.attrs[2], text, (uint16_t)len);
}

int ble_adc_send_gesture(gesture_id_t gesture, const uint16_t adc[4])
{
    char buf[20];
    int  len;

    ARG_UNUSED(adc); /* ADC trimis separat via ble_adc_send_values() */

    if ((current_conn == NULL) || !notify_enabled) {
        return -ENOTCONN;
    }

    /* Format: "G:1 FIST\n" - ID + name, max 14 bytes, sub limita MTU 20. */
    len = snprintf(buf, sizeof(buf), "G:%u %s\n",
                   (unsigned)gesture, gesture_name(gesture));
    if ((len <= 0) || (len >= (int)sizeof(buf))) {
        return -EINVAL;
    }

    return bt_gatt_notify(current_conn, &nus_svc.attrs[2], buf, (uint16_t)len);
}
