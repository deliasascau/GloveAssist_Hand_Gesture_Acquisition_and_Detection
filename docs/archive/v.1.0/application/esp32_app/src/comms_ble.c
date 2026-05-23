/**
 * @file comms_ble.c
 * @brief BLE NUS (Nordic UART Service) gateway on ESP32
 *
 * Receives gesture/sensor data from STM32 via UART, forwards to
 * connected BLE Central as GATT notifications.
 * Also sends commands from BLE Central back to STM32 via UART.
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "frame_protocol.h"
#include "uart_comm.h"
#include "comms_ble.h"

LOG_MODULE_REGISTER(comms_ble, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- NUS UUIDs ---------- */
static struct bt_uuid_128 nus_svc_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

static struct bt_uuid_128 nus_tx_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

static struct bt_uuid_128 nus_rx_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

/* ---------- Connection state ---------- */
static struct bt_conn *current_conn;
static bool ble_notif_enabled;

/* ---------- GATT callbacks ---------- */
static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ble_notif_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("BLE notifications %s", ble_notif_enabled ? "enabled" : "disabled");
}

static ssize_t on_rx_write(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr,
                           const void *buf, uint16_t len,
                           uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (len > PROTO_PAYLOAD_SIZE) {
        LOG_WRN("BLE RX: payload too large (%u)", len);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    /* Minimum 1 byte: cmd_id */
    if (len < 1U) {
        LOG_WRN("BLE RX: payload gol — ignorat");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    LOG_INF("BLE RX: %u bytes de la central", len);

    /* Text shortcut: phone sends ASCII "inteles\n" or "inteles" →
     * auto-map to CMD_ACK_RECEIVED for user-friendly app integration */
    {
        const uint8_t *text = (const uint8_t *)buf;
        bool is_ack_text = false;

        if ((len >= 7U) && (memcmp(text, "inteles", 7U) == 0)) {
            is_ack_text = true;
        } else if ((len >= 3U) && (memcmp(text, "ack", 3U) == 0)) {
            is_ack_text = true;
        } else if ((len >= 2U) && (memcmp(text, "ok", 2U) == 0)) {
            is_ack_text = true;
        }

        if (is_ack_text) {
            frame_command_payload_t ack_cmd;
            (void)memset(&ack_cmd, 0, sizeof(ack_cmd));
            ack_cmd.cmd_id = CMD_ACK_RECEIVED;
            int ack_rc = uart_comm_send_command(&ack_cmd);
            if (ack_rc < 0) {
                LOG_ERR("CMD_ACK_RECEIVED relay failed: %d", ack_rc);
                return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
            }
            {
                const char ack_ok[] = "OK:ACK relayed\n";
                (void)ble_send_notification((const uint8_t *)ack_ok,
                                            (uint16_t)(sizeof(ack_ok) - 1U));
            }
            LOG_INF("BLE text ACK -> CMD_ACK_RECEIVED relayed to STM32");
            return (ssize_t)len;
        }
    }

    /* Populeaza structura comanda din bytes primiti de la telefon.
     * Format asteptat: [cmd_id 1B][finger_idx 1B][value_lo 1B][value_hi 1B]
     * Bytes lipsa → raman 0 (zero-init prin memset). */
    frame_command_payload_t cmd;
    (void)memset(&cmd, 0, sizeof(cmd));

    const uint8_t *data = (const uint8_t *)buf;
    cmd.cmd_id = data[0];
    if (len >= 2U) {
        cmd.finger_idx = data[1];
    }
    if (len >= 4U) {
        /* value little-endian: [value_lo, value_hi] */
        cmd.value = (uint16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8U));
    }

    if (cmd.cmd_id == CMD_SNIFF_REPORT) {
        int local_rc = uart_comm_send_sniff_report();
        if (local_rc < 0) {
            LOG_ERR("SNIFF report send failed: %d", local_rc);
            return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
        }
        LOG_INF("SNIFF report sent to BLE central");
        return (ssize_t)len;
    }

    int rc = uart_comm_send_command(&cmd);
    if (rc < 0) {
        LOG_ERR("Relay comanda la STM32 esuat: %d", rc);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return (ssize_t)len;
}

/* ---------- GATT service definition ---------- */
BT_GATT_SERVICE_DEFINE(nus_svc,
    BT_GATT_PRIMARY_SERVICE(&nus_svc_uuid),

    /* TX characteristic (notify: ESP32 -> Phone) */
    BT_GATT_CHARACTERISTIC(&nus_tx_uuid.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    /* RX characteristic (write: Phone -> ESP32) */
    BT_GATT_CHARACTERISTIC(&nus_rx_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, on_rx_write, NULL),
);

/* ---------- Connection callbacks ---------- */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("BLE connection failed: 0x%02x", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    LOG_INF("BLE connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("BLE disconnected (reason 0x%02x)", reason);
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    ble_notif_enabled = false;
}

BT_CONN_CB_DEFINE(conn_cb) = {
    .connected    = connected,
    .disconnected = disconnected,
};

/* ---------- Advertising data (defined before SMP callbacks that reference them) ---------- */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, "GloveAssist", sizeof("GloveAssist") - 1),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)),
};

static const struct bt_le_adv_param *const adv_params =
    BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
                    BT_GAP_ADV_FAST_INT_MIN_2,
                    BT_GAP_ADV_FAST_INT_MAX_2,
                    NULL);

#if defined(CONFIG_BT_SMP)
/* ---------- SMP pairing callbacks ---------- */
static void auth_cancel(struct bt_conn *conn)
{
    LOG_WRN("BLE pairing cancelled");
    ARG_UNUSED(conn);
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    ARG_UNUSED(conn);
    LOG_INF("BLE pairing passkey: %06u", passkey);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    LOG_INF("BLE pairing complete (bonded=%d)", bonded);
    ARG_UNUSED(conn);
}

static void pairing_failed(struct bt_conn *conn,
                            enum bt_security_err reason)
{
    LOG_WRN("BLE pairing failed (reason %d) — will retry", reason);
    ARG_UNUSED(conn);
    /* Re-advertise after pairing failure */
    (void)bt_le_adv_start(adv_params, ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
}

static struct bt_conn_auth_cb auth_cb = {
    .passkey_display = auth_passkey_display,
    .cancel          = auth_cancel,
};

static struct bt_conn_auth_info_cb auth_info_cb = {
    .pairing_complete = pairing_complete,
    .pairing_failed   = pairing_failed,
};
#endif

/* ---------- Public API ---------- */

int ble_init(void)
{
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed: %d", err);
        return err;
    }

    LOG_INF("BLE initialised");

#if defined(CONFIG_BT_SMP)
    /* Register SMP (Just Works pairing) */
    err = bt_conn_auth_cb_register(&auth_cb);
    if (err) {
        LOG_WRN("auth_cb register failed: %d", err);
    }
    err = bt_conn_auth_info_cb_register(&auth_info_cb);
    if (err) {
        LOG_WRN("auth_info_cb register failed: %d", err);
    }
#endif

    err = bt_le_adv_start(adv_params, ad, ARRAY_SIZE(ad),
                          sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising start failed: %d", err);
        return err;
    }

    LOG_INF("BLE advertising as 'GloveAssist'");
    return 0;
}

int ble_send_notification(const uint8_t *data, uint16_t len)
{
    int32_t result;

    if (!current_conn || !ble_notif_enabled) {
        result = -ENOTCONN;
    } else {
        struct bt_gatt_notify_params params = {
            .uuid = &nus_tx_uuid.uuid,
            .attr = &nus_svc.attrs[1],
            .data = data,
            .len  = len,
        };
        result = bt_gatt_notify_cb(current_conn, &params);
    }

    return result;
}

bool ble_is_connected(void)
{
    return (current_conn != NULL);
}

uint8_t ble_get_rssi(void)
{
    /* RSSI reading requires bt_conn_get_info — simplified stub for Phase 2 */
    return 0U;
}

int ble_send_raw_adc_text(const uint16_t raw[4])
{
    /* Format: "I:1234 M:2048 R:3012 P:0512\n" (max 29 chars, fits in BLE MTU) */
    char    buf[32];
    int     n;

    n = snprintf(buf, sizeof(buf), "I:%04u M:%04u R:%04u P:%04u\n",
                 (unsigned int)raw[0], (unsigned int)raw[1],
                 (unsigned int)raw[2], (unsigned int)raw[3]);

    if (n <= 0) {
        return -EINVAL;
    }

    return ble_send_notification((const uint8_t *)buf, (uint16_t)n);
}
