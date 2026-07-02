#include "ble_adc.h"

#if defined(CONFIG_BT)

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include "frame_protocol.h"
#include "gesture.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(ble_adc, LOG_LEVEL_INF);

/* Last confirmed gesture — set by main, read by rx_write. */
static gesture_id_t s_last_gesture = GESTURE_NONE;

/* BLE command flags — set by rx_write, consumed by main loop. */
static volatile bool        s_calibration_requested;
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
static bool link_encrypted;
static bool security_needed;
static bool security_pause_started;
static uint32_t next_security_request_ms;

#define BLE_SECURITY_DELAY_MS        1500U
#define BLE_SECURITY_PAUSE_SETTLE_MS 800U
#define BLE_SECURITY_MQTT_PAUSE_MS   10000U

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

static volatile bool s_send_welcome;  /* set in ccc_changed, sent from main loop */

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);

    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("BLE notify %s", notify_enabled ? "enabled" : "disabled");

    if (notify_enabled) {
        s_send_welcome = true;
    }
}

static ssize_t rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset,
                        uint8_t flags)
{
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (bt_conn_get_security(conn) < BT_SECURITY_L2) {
        LOG_WRN("BLE RX rejected: link not encrypted");
        return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION);
    }

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

/* 500-1000 ms advertising: easier on ESP32 WiFi/BLE coexistence than
 * BT_LE_ADV_CONN_FAST_1, while still discoverable within a few seconds. */
#define BLE_ADV_INTERVAL_MIN 0x0320U
#define BLE_ADV_INTERVAL_MAX 0x0640U

static const struct bt_le_adv_param s_adv_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN,
                         BLE_ADV_INTERVAL_MIN,
                         BLE_ADV_INTERVAL_MAX,
                         NULL);

static void adv_retry_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(s_adv_retry_work, adv_retry_work_handler);

static int ble_start_advertising(void)
{
    int err = bt_le_adv_start(&s_adv_param, ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));

    if ((err == 0) || (err == -EALREADY)) {
        LOG_INF("BLE advertising as GloveAssist (coexist slow)");
        return 0;
    }

    LOG_WRN("BLE advertising start failed: %d", err);
    return err;
}

static void adv_retry_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    int err = ble_start_advertising();

    if ((err == -ENOMEM) || (err == -EAGAIN) || (err == -EBUSY)) {
        (void)k_work_reschedule(&s_adv_retry_work, K_MSEC(1000));
    }
}

BT_GATT_SERVICE_DEFINE(nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_svc_uuid),
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    /* CCC is only a subscription flag. Keep it writable before encryption so
     * phone apps do not fail with "reconnect to subscribe". */
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    /* RX writable without link-layer encryption: ESP32 RAM is too tight to
     * run SMP + WiFi/TLS simultaneously without crashing.  Caregiver ACK
     * is not safety-critical; the actual security is the UART AES+HMAC link
     * between STM32 and ESP32.  If the phone OS auto-pairs, the bond is
     * stored and future reconnects re-encrypt transparently. */
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

    if (current_conn != NULL) {
        bt_conn_unref(current_conn);
    }
    current_conn = bt_conn_ref(conn);
    notify_enabled = false;
    link_encrypted = false;
    security_needed = true;
    /*
     * Immediately disconnect WiFi so BLE Secure Connections (P-256 ECC) has
     * enough heap to complete pairing.  ESP32 cannot run SC + WiFi/TLS
     * simultaneously without exhausting the shared heap → panic reset.
     * wifi_mqtt_reconnect_after_ble_pairing() re-enables WiFi once SMP
     * finishes (called from security_changed and disconnected).
     */
    wifi_mqtt_disconnect_for_ble_pairing();
    security_pause_started = true;   /* WiFi already paused; skip ble_adc_process duplicate */
    next_security_request_ms = k_uptime_get_32() + BLE_SECURITY_DELAY_MS
                               + BLE_SECURITY_PAUSE_SETTLE_MS;
    (void)k_work_cancel_delayable(&s_adv_retry_work);

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
    link_encrypted = false;
    security_needed = false;
    security_pause_started = false;
    next_security_request_ms = 0U;

    /* Restore WiFi if it was disconnected for BLE pairing. */
    wifi_mqtt_reconnect_after_ble_pairing();

    /* Restart outside the stack callback. On ESP32+WiFi, immediate restart can
     * return -ENOMEM while BT/WiFi buffers are still being released. */
    (void)k_work_reschedule(&s_adv_retry_work, K_MSEC(500));
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    if (err != BT_SECURITY_ERR_SUCCESS) {
        link_encrypted = false;
        security_needed = true;
        security_pause_started = false;

        if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING) {
            /*
             * Phone has a cached LTK from a previous session but ESP32's NVS
             * was cleared (e.g. firmware flash with --erase).  Clear the
             * stale bond on our side and disconnect so the phone reconnects
             * and triggers a fresh SMP Just Works pairing.
             */
            LOG_WRN("BLE stale key: phone LTK not found locally — clearing bond");
            (void)bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));            wifi_mqtt_reconnect_after_ble_pairing();            (void)bt_conn_disconnect(conn, BT_HCI_ERR_UNSPECIFIED);
            return;
        }

        next_security_request_ms = k_uptime_get_32() + 5000U;
        wifi_mqtt_reconnect_after_ble_pairing();
        LOG_WRN("BLE security failed (level %d, err %d/%s) - link not encrypted",
                (int)level, (int)err, bt_security_err_to_str(err));
        return;
    }
    link_encrypted = (level >= BT_SECURITY_L2);
    security_needed = !link_encrypted;
    security_pause_started = false;
    /* Pairing complete — allow WiFi to reconnect. */
    wifi_mqtt_reconnect_after_ble_pairing();
    LOG_INF("BLE link encrypted (security level %d)", (int)level);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);

    LOG_INF("BLE pairing complete (%s)", bonded ? "bonded" : "non-bonded");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    LOG_WRN("BLE pairing failed: %d/%s", (int)reason,
            bt_security_err_to_str(reason));
    /*
     * Remove any partial or stale bond so the next connection attempt starts
     * a fresh SMP exchange instead of cycling on a mismatched LTK.
     */
    (void)bt_unpair(BT_ID_DEFAULT, bt_conn_get_dst(conn));
}

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected       = connected,
    .disconnected    = disconnected,
    .security_changed = security_changed,
};

/* Slow advertising (500ms min, 1000ms max) gives WiFi enough air time when
 * BLE+WiFi coexistence is active on ESP32. Still fast enough to be found
 * by LightBlue/nRF Connect within 2-3 seconds.
 * 800 * 0.625 ms = 500 ms, 1600 * 0.625 ms = 1000 ms */

int ble_adc_init(void)
{
    int err = bt_enable(NULL);

    if (err != 0) {
        LOG_ERR("bt_enable failed: %d", err);
        return err;
    }

    /*
     * Load saved bond keys from NVS.  settings_subsys_init() is idempotent;
     * calling it here is safe even if calibration_init() calls it later.
     * Without this, bt_enable() registers the BT settings handler but the
     * stored LTK is never loaded, so every reconnect still requires re-pairing.
     */
    (void)settings_subsys_init();
    (void)settings_load_subtree("bt");

    err = bt_conn_auth_info_cb_register(&auth_info_cb);
    if (err != 0) {
        LOG_WRN("BLE auth info callback register failed: %d", err);
    }

    err = ble_start_advertising();
    if (err != 0) {
        return err;
    }

    return 0;
}

void ble_adc_process(void)
{
    uint32_t now;
    int err;

    /* Send welcome confirmation when phone first subscribes to notifications. */
    if (s_send_welcome && notify_enabled && (current_conn != NULL)) {
        s_send_welcome = false;
        (void)ble_adc_send_text("GloveAssist OK\n");
    }

    if ((current_conn == NULL) || link_encrypted || !security_needed) {
        return;
    }

    now = k_uptime_get_32();
    if ((next_security_request_ms != 0U)
        && ((int32_t)(now - next_security_request_ms) < 0)) {
        return;
    }

    if (!security_pause_started) {
        wifi_mqtt_pause_for_ble_security(BLE_SECURITY_MQTT_PAUSE_MS);
        security_pause_started = true;
        next_security_request_ms = now + BLE_SECURITY_PAUSE_SETTLE_MS;
        LOG_INF("BLE security pending - pausing MQTT/TLS before pairing");
        return;
    }

    err = bt_conn_set_security(current_conn, BT_SECURITY_L2);
    if (err == 0) {
        LOG_INF("BLE security requested");
        next_security_request_ms = now + 5000U;
        return;
    }

    if ((err == -EALREADY) || (err == -EBUSY)) {
        next_security_request_ms = now + 1000U;
        return;
    }

    LOG_WRN("BLE security request failed: %d", err);
    security_pause_started = false;
    next_security_request_ms = now + 3000U;
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

bool ble_adc_pop_caregiver_ack_request(gesture_id_t *out_gesture)
{
    if (!s_caregiver_ack_requested) {
        return false;
    }
    s_caregiver_ack_requested = false;
    if (out_gesture != NULL) {
        *out_gesture = s_gesture_at_ack;
    }
    return true;
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

#endif /* CONFIG_BT */
