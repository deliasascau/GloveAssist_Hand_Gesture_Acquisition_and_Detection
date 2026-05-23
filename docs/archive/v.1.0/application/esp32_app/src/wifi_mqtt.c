/**
 * @file wifi_mqtt.c
 * @brief WiFi connection + Adafruit IO MQTT publishing (ESP32, Zephyr 4.4)
 *
 * Flow:
 *   1. Connect WPA2-PSK via net_mgmt(NET_REQUEST_WIFI_CONNECT)
 *   2. Obtain IP via DHCP
 *   3. Resolve io.adafruit.com DNS
 *   4. Register CA certificate and connect MQTT client over TLS (port 8883)
 *   5. Drain internal message queue → publish to Adafruit IO feeds
 *   6. Keepalive ping every MQTT_KEEPALIVE_S/2 seconds
 *   7. Reconnect on WiFi drop / MQTT disconnect (exponential backoff)
 *
 * Credentials: set WIFI_SSID, WIFI_PSK, ADAFRUIT_IO_USERNAME, ADAFRUIT_IO_KEY
 *              in application/common/include/app_config.h before flashing.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#if defined(CONFIG_MQTT_LIB_TLS)
#include <zephyr/net/tls_credentials.h>
#endif
#include <string.h>
#include <stdio.h>

#if defined(__has_include)
#if __has_include("app_secrets.h")
#include "app_secrets.h"  /* Optional local credentials, not tracked in git. */
#endif
#endif
#include "app_config.h"
#include "wifi_mqtt.h"

LOG_MODULE_REGISTER(wifi_mqtt, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Internal publish message queue ---------- */
typedef enum { MSG_GESTURE = 0U, MSG_STATUS = 1U } mqtt_msg_type_t;

typedef struct {
    mqtt_msg_type_t type;
    union {
        struct { uint8_t gesture_id; uint8_t confidence; } gesture;
        struct { bool    esp32_alive; bool    ble_conn;  } status;
    };
} mqtt_msg_t;

K_MSGQ_DEFINE(s_mqtt_msgq, sizeof(mqtt_msg_t), 8U, 4U);

/* ---------- WiFi + IP synchronization semaphores ---------- */
static K_SEM_DEFINE(s_wifi_sem, 0, 1);
static K_SEM_DEFINE(s_ipv4_sem, 0, 1);

static struct net_mgmt_event_callback s_wifi_cb;
static struct net_mgmt_event_callback s_ipv4_cb;
static bool s_cbs_registered    = false;
static volatile bool s_wifi_up  = false;
static volatile bool s_mqtt_connected = false;

/* ---------- MQTT client state ---------- */
static struct mqtt_client s_client;
static uint8_t            s_rx_buf[256];
static uint8_t            s_tx_buf[256];
static struct sockaddr_in s_broker_addr;

#if defined(CONFIG_MQTT_LIB_TLS)
static const sec_tag_t s_mqtt_sec_tags[] = {
    (sec_tag_t)MQTT_TLS_SEC_TAG,
};
#endif

/* Gesture name table (same mapping as sensor_logic.c) */
static const char *const k_gesture_names[] = {
    [GESTURE_NONE]   = "NONE",
    [GESTURE_INDEX]  = "INDEX",
    [GESTURE_MIDDLE] = "MIDDLE",
    [GESTURE_RING]   = "RING",
    [GESTURE_PINKY]  = "PINKY",
    [GESTURE_FIST]   = "FIST",
    [GESTURE_HELP]   = "HELP",
};

static bool mqtt_configured(void)
{
    if ((strcmp(WIFI_SSID, "YOUR_WIFI_SSID") == 0) ||
        (strcmp(WIFI_PSK, "YOUR_WIFI_PASSWORD") == 0) ||
        (strcmp(ADAFRUIT_IO_USERNAME, "YOUR_AIO_USERNAME") == 0) ||
        (strcmp(ADAFRUIT_IO_KEY, "YOUR_AIO_KEY") == 0)) {
        return false;
    }

#if defined(CONFIG_MQTT_LIB_TLS)
    if ((ADAFRUIT_IO_CA_CERT_PEM[0] == '\0') ||
        (strstr(ADAFRUIT_IO_CA_CERT_PEM, "BEGIN CERTIFICATE") == NULL)) {
        return false;
    }
#endif

    return true;
}

#if defined(CONFIG_MQTT_LIB_TLS)
static int mqtt_tls_init(void)
{
    const size_t cert_len = strlen(ADAFRUIT_IO_CA_CERT_PEM) + 1U;
    int rc = tls_credential_add((sec_tag_t)MQTT_TLS_SEC_TAG,
                                TLS_CREDENTIAL_CA_CERTIFICATE,
                                ADAFRUIT_IO_CA_CERT_PEM, cert_len);

    if (rc == -EEXIST) {
        return 0;
    }
    if (rc < 0) {
        LOG_ERR("TLS CA registration failed: %d", rc);
        return rc;
    }

    LOG_INF("MQTT TLS CA registered");
    return 0;
}
#endif

static int mqtt_socket_fd(const struct mqtt_client *client)
{
#if defined(CONFIG_MQTT_LIB_TLS)
    if (client->transport.type == MQTT_TRANSPORT_SECURE) {
        return client->transport.tls.sock;
    }
#endif

    return client->transport.tcp.sock;
}

/* ---------- Network event callbacks ---------- */
static void wifi_event_cb(struct net_mgmt_event_callback *cb,
                          uint64_t event, struct net_if *iface)
{
    ARG_UNUSED(iface);
    if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *st = (const struct wifi_status *)cb->info;
        if (st->conn_status == WIFI_STATUS_CONN_SUCCESS) {
            LOG_INF("WiFi connected: %s", WIFI_SSID);
            s_wifi_up = true;
            k_sem_give(&s_wifi_sem);
        } else {
            LOG_WRN("WiFi connect failed: %d", (int)st->conn_status);
        }
    } else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_WRN("WiFi disconnected");
        s_wifi_up = false;
    }
}

static void ipv4_event_cb(struct net_mgmt_event_callback *cb,
                          uint64_t event, struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);
    if (event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("IP address acquired");
        k_sem_give(&s_ipv4_sem);
    }
}

static void register_net_callbacks(void)
{
    if (!s_cbs_registered) {
        net_mgmt_init_event_callback(&s_wifi_cb, wifi_event_cb,
            NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
        net_mgmt_add_event_callback(&s_wifi_cb);

        net_mgmt_init_event_callback(&s_ipv4_cb, ipv4_event_cb,
            NET_EVENT_IPV4_ADDR_ADD);
        net_mgmt_add_event_callback(&s_ipv4_cb);

        s_cbs_registered = true;
    }
}

/* ---------- WiFi connect + DHCP ---------- */
static int wifi_connect_ap(void)
{
    struct net_if *iface = net_if_get_default();
    if (iface == NULL) {
        LOG_ERR("No default network interface");
        return -ENODEV;
    }

    register_net_callbacks();

    struct wifi_connect_req_params params = {
        .ssid        = (const uint8_t *)WIFI_SSID,
        .ssid_length = (uint8_t)strlen(WIFI_SSID),
        .psk         = (const uint8_t *)WIFI_PSK,
        .psk_length  = (uint8_t)strlen(WIFI_PSK),
        .security    = WIFI_SECURITY_TYPE_PSK,
        .channel     = WIFI_CHANNEL_ANY,
        .band        = WIFI_FREQ_BAND_2_4_GHZ,
        .mfp         = WIFI_MFP_OPTIONAL,
    };

    int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
                      &params, sizeof(params));
    if (rc != 0) {
        LOG_ERR("WiFi connect request: %d", rc);
        return rc;
    }

    if (k_sem_take(&s_wifi_sem, K_SECONDS(WIFI_CONNECT_TIMEOUT_S)) != 0) {
        LOG_ERR("WiFi connect timeout");
        return -ETIMEDOUT;
    }

    net_dhcpv4_start(iface);

    if (k_sem_take(&s_ipv4_sem, K_SECONDS(15)) != 0) {
        LOG_ERR("DHCP timeout");
        return -ETIMEDOUT;
    }

    return 0;
}

/* ---------- DNS resolution ---------- */
static int resolve_broker(void)
{
    struct zsock_addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct zsock_addrinfo *res = NULL;
    char port_str[8];

    snprintf(port_str, sizeof(port_str), "%u", (unsigned)ADAFRUIT_IO_PORT);

    int rc = zsock_getaddrinfo(ADAFRUIT_IO_HOST, port_str, &hints, &res);
    if (rc != 0) {
        LOG_ERR("DNS resolve failed for %s: %d", ADAFRUIT_IO_HOST, rc);
        return -EIO;
    }

    memcpy(&s_broker_addr, res->ai_addr, sizeof(struct sockaddr_in));
    zsock_freeaddrinfo(res);

    LOG_INF("Broker %s resolved", ADAFRUIT_IO_HOST);
    return 0;
}

/* ---------- MQTT event handler ---------- */
static void mqtt_evt_handler(struct mqtt_client *client,
                             const struct mqtt_evt *evt)
{
    ARG_UNUSED(client);
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result == 0) {
            LOG_INF("MQTT CONNACK — broker ready");
            s_mqtt_connected = true;
        } else {
            LOG_ERR("MQTT CONNACK error: %d", evt->result);
        }
        break;
    case MQTT_EVT_DISCONNECT:
        LOG_WRN("MQTT disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVT_PINGRESP:
        break;
    default:
        break;
    }
}

/* ---------- MQTT client configuration ---------- */
static void mqtt_client_configure(void)
{
    static const struct mqtt_utf8 s_user = {
        .utf8 = (const uint8_t *)ADAFRUIT_IO_USERNAME,
        .size = sizeof(ADAFRUIT_IO_USERNAME) - 1U,
    };
    static const struct mqtt_utf8 s_pass = {
        .utf8 = (const uint8_t *)ADAFRUIT_IO_KEY,
        .size = sizeof(ADAFRUIT_IO_KEY) - 1U,
    };

    mqtt_client_init(&s_client);

    s_client.broker              = (struct sockaddr *)&s_broker_addr;
    s_client.evt_cb              = mqtt_evt_handler;
    s_client.client_id.utf8      = (const uint8_t *)MQTT_CLIENT_ID;
    s_client.client_id.size      = sizeof(MQTT_CLIENT_ID) - 1U;
    s_client.user_name           = (struct mqtt_utf8 *)&s_user;
    s_client.password            = (struct mqtt_utf8 *)&s_pass;
    s_client.protocol_version    = MQTT_VERSION_3_1_1;
    s_client.keepalive           = MQTT_KEEPALIVE_S;
    s_client.rx_buf              = s_rx_buf;
    s_client.rx_buf_size         = sizeof(s_rx_buf);
    s_client.tx_buf              = s_tx_buf;
    s_client.tx_buf_size         = sizeof(s_tx_buf);
#if defined(CONFIG_MQTT_LIB_TLS)
    s_client.transport.type      = MQTT_TRANSPORT_SECURE;
    s_client.transport.tls.config.peer_verify = TLS_PEER_VERIFY_REQUIRED;
    s_client.transport.tls.config.cipher_list = NULL;
    s_client.transport.tls.config.sec_tag_list = s_mqtt_sec_tags;
    s_client.transport.tls.config.sec_tag_count = ARRAY_SIZE(s_mqtt_sec_tags);
    s_client.transport.tls.config.hostname = ADAFRUIT_IO_HOST;
#else
    s_client.transport.type      = MQTT_TRANSPORT_NON_SECURE;
#endif
}

/* ---------- MQTT publish helper (QoS 0) ---------- */
static int mqtt_pub_str(const char *topic, const char *payload)
{
    struct mqtt_publish_param p = {
        .message = {
            .topic = {
                .topic = {
                    .utf8 = (const uint8_t *)topic,
                    .size = (uint32_t)strlen(topic),
                },
                .qos = MQTT_QOS_0_AT_MOST_ONCE,
            },
            .payload = {
                .data = (uint8_t *)payload,
                .len  = (uint32_t)strlen(payload),
            },
        },
        .message_id  = 1U,
        .dup_flag    = 0U,
        .retain_flag = 0U,
    };
    return mqtt_publish(&s_client, &p);
}

/* ---------- Process one message from the internal queue ---------- */
static void process_msg(const mqtt_msg_t *msg)
{
    char payload[64];
    int  rc;

    if (msg->type == MSG_GESTURE) {
        const char *name = (msg->gesture.gesture_id <= GESTURE_MAX_ID)
                           ? k_gesture_names[msg->gesture.gesture_id]
                           : "UNKNOWN";
        snprintf(payload, sizeof(payload), "%s:%u",
                 name, (unsigned)msg->gesture.confidence);
        rc = mqtt_pub_str(ADAFRUIT_IO_FEED_GESTURE, payload);
    } else {
        snprintf(payload, sizeof(payload), "ALIVE:%d,BLE:%d",
                 (int)msg->status.esp32_alive, (int)msg->status.ble_conn);
        rc = mqtt_pub_str(ADAFRUIT_IO_FEED_STATUS, payload);
    }

    if (rc != 0) {
        LOG_WRN("MQTT publish failed: %d", rc);
    } else {
        LOG_INF("MQTT published: %s", payload);
    }
}

/* ---------- Thread entry ---------- */
void wifi_mqtt_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    LOG_INF("WiFi/MQTT thread started");

    if (!mqtt_configured()) {
        LOG_WRN("WiFi/MQTT disabled: configure WiFi, Adafruit IO and TLS CA in app_config.h");
        return;
    }

#if defined(CONFIG_MQTT_LIB_TLS)
    int rc = mqtt_tls_init();
    if (rc != 0) {
        LOG_ERR("WiFi/MQTT disabled: TLS setup failed");
        return;
    }
#endif

    /* Step 1 — WiFi with exponential backoff */
    uint32_t backoff_ms = 2000U;
    while (wifi_connect_ap() != 0) {
        LOG_WRN("WiFi retry in %u ms", backoff_ms);
        k_msleep(backoff_ms);
        backoff_ms = MIN(backoff_ms * 2U, 60000U);
    }

    /* Step 2 — DNS resolution */
    while (resolve_broker() != 0) {
        k_msleep(5000U);
    }

    /* Step 3 — MQTT connect + publish loop */
    backoff_ms = 2000U;
    while (1) {
        s_mqtt_connected = false;
        mqtt_client_configure();

        int rc = mqtt_connect(&s_client);
        if (rc != 0) {
            LOG_ERR("mqtt_connect: %d — retry %u ms", rc, backoff_ms);
            k_msleep(backoff_ms);
            backoff_ms = MIN(backoff_ms * 2U, 60000U);
            continue;
        }

        /* Poll for CONNACK (up to 5 s) */
        struct zsock_pollfd fds = {
            .fd     = mqtt_socket_fd(&s_client),
            .events = ZSOCK_POLLIN,
        };
        for (int i = 0; i < 10 && !s_mqtt_connected; i++) {
            if (zsock_poll(&fds, 1, 500) > 0) {
                mqtt_input(&s_client);
            }
        }

        if (!s_mqtt_connected) {
            LOG_ERR("MQTT CONNACK timeout");
            mqtt_disconnect(&s_client, NULL);
            k_msleep(backoff_ms);
            backoff_ms = MIN(backoff_ms * 2U, 60000U);
            continue;
        }
        backoff_ms = 2000U;
        LOG_INF("MQTT ready — publishing to Adafruit IO");

        int64_t last_ping_ms = k_uptime_get();

        while (s_mqtt_connected && s_wifi_up) {
            mqtt_msg_t msg;
            bool got_msg = (k_msgq_get(&s_mqtt_msgq, &msg, K_MSEC(400)) == 0);

            /* Keepalive ping */
            int64_t now = k_uptime_get();
            if ((now - last_ping_ms) >= (int64_t)(MQTT_KEEPALIVE_S * 500U)) {
                mqtt_live(&s_client);
                last_ping_ms = now;
            }

            /* Drain socket (PUBACK / PINGRESP) */
            if (zsock_poll(&fds, 1, 0) > 0) {
                mqtt_input(&s_client);
            }

            if (got_msg) {
                process_msg(&msg);
            }
        }

        LOG_WRN("MQTT event loop exited — reconnecting");
        mqtt_disconnect(&s_client, NULL);
        k_msleep(2000U);

        /* Reconnect WiFi if it dropped */
        if (!s_wifi_up) {
            k_sem_reset(&s_wifi_sem);
            k_sem_reset(&s_ipv4_sem);
            backoff_ms = 2000U;
            while (wifi_connect_ap() != 0) {
                k_msleep(backoff_ms);
                backoff_ms = MIN(backoff_ms * 2U, 60000U);
            }
        }
    }
}

/* ---------- Public API ---------- */
int wifi_mqtt_publish_gesture(uint8_t gesture_id, uint8_t confidence)
{
    mqtt_msg_t msg = {
        .type    = MSG_GESTURE,
        .gesture = { .gesture_id = gesture_id, .confidence = confidence },
    };
    return k_msgq_put(&s_mqtt_msgq, &msg, K_NO_WAIT);
}

int wifi_mqtt_publish_status(bool esp32_alive, bool ble_conn)
{
    mqtt_msg_t msg = {
        .type   = MSG_STATUS,
        .status = { .esp32_alive = esp32_alive, .ble_conn = ble_conn },
    };
    return k_msgq_put(&s_mqtt_msgq, &msg, K_NO_WAIT);
}

bool wifi_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}
