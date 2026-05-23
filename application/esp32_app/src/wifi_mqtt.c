/*
 * wifi_mqtt.c - WiFi + MQTT cloud publish for GloveAssist.
 *
 * Compiled only when CONFIG_WIFI and CONFIG_MQTT_LIB are enabled.
 * When disabled (default BLE-only build), stub implementations are compiled
 * so the rest of the codebase can call wifi_mqtt_* unconditionally.
 *
 * MQTT transport modes:
 *   Plain TCP (default):  port 1883, MQTT_TRANSPORT_NON_SECURE
 *   TLS 1.2/1.3:         port 8883, MQTT_TRANSPORT_SECURE
 *                         Enable: CONFIG_GLOVE_MQTT_TLS=y in prj_wifi_mqtt.conf
 *                         Cert:   certs/mosquitto_ca.pem (embedded at build time)
 *
 * Thread safety: all functions must be called from the main thread only.
 * wifi_mqtt_process() handles keepalive pings and input draining.
 */

#include "wifi_mqtt.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi_mqtt, LOG_LEVEL_INF);

#if defined(CONFIG_WIFI) && defined(CONFIG_MQTT_LIB)

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#if defined(CONFIG_GLOVE_MQTT_TLS)
#include <zephyr/net/tls_credentials.h>

/* CA certificate embedded from certs/mosquitto_ca.pem at build time.
 * generate_inc_file_for_target() produces a comma-separated byte sequence. */
static const uint8_t s_ca_cert[] = {
#include "mosquitto_ca.pem.inc"
};
#define TLS_SEC_TAG  CONFIG_GLOVE_MQTT_TLS_SEC_TAG
#endif /* CONFIG_GLOVE_MQTT_TLS */

/* ── Configuration (from Kconfig / prj_wifi_mqtt.conf) ─────────────── */

#ifndef CONFIG_GLOVE_MQTT_BROKER_HOST
#define CONFIG_GLOVE_MQTT_BROKER_HOST "test.mosquitto.org"
#endif

#ifndef CONFIG_GLOVE_MQTT_BROKER_PORT
#define CONFIG_GLOVE_MQTT_BROKER_PORT 1883
#endif

#define CLIENT_ID       "GloveAssist"
#define TOPIC_GESTURE   "gloveassist/gesture"
#define TOPIC_ADC       "gloveassist/adc"
#define MQTT_BUF_SIZE   256U
#define CONNECT_RETRY_MS 5000U   /* retry MQTT connect every 5 s */

/* ── MQTT client state ──────────────────────────────────────────────── */

static uint8_t s_rx_buf[MQTT_BUF_SIZE];
static uint8_t s_tx_buf[MQTT_BUF_SIZE];

static struct mqtt_client         s_client;
static struct sockaddr_storage    s_broker_addr;
static bool                       s_mqtt_connected;
static bool                       s_wifi_connected;
static uint32_t                   s_last_connect_attempt;

/* ── WiFi / IPv4 event callbacks ────────────────────────────────────── */

static struct net_mgmt_event_callback s_wifi_cb;
static struct net_mgmt_event_callback s_ipv4_cb;

static void wifi_evt_handler(struct net_mgmt_event_callback *cb,
                             uint32_t event,
                             struct net_if *iface)
{
    ARG_UNUSED(iface);

    if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *st = (const struct wifi_status *)cb->info;

        if (st->conn_status == WIFI_STATUS_CONN_SUCCESS) {
            LOG_INF("WiFi associated - waiting for DHCP");
        } else {
            LOG_WRN("WiFi connect failed (status %d)", st->conn_status);
        }
    } else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        LOG_INF("WiFi disconnected");
        s_wifi_connected = false;
        s_mqtt_connected = false;
    }
}

static void ipv4_evt_handler(struct net_mgmt_event_callback *cb,
                             uint32_t event,
                             struct net_if *iface)
{
    ARG_UNUSED(cb);
    ARG_UNUSED(iface);

    if (event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("IPv4 address acquired - WiFi ready");
        s_wifi_connected = true;
    }
}

/* ── MQTT event handler ─────────────────────────────────────────────── */

static void mqtt_evt_handler(struct mqtt_client *client,
                             const struct mqtt_evt *evt)
{
    ARG_UNUSED(client);

    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result == 0) {
            s_mqtt_connected = true;
            LOG_INF("MQTT connected to %s:%d",
                    CONFIG_GLOVE_MQTT_BROKER_HOST,
                    CONFIG_GLOVE_MQTT_BROKER_PORT);
        } else {
            LOG_ERR("MQTT CONNACK error: %d", evt->result);
        }
        break;

    case MQTT_EVT_DISCONNECT:
        s_mqtt_connected = false;
        LOG_INF("MQTT disconnected");
        break;

    case MQTT_EVT_PUBACK:
        /* QoS 1 publish acknowledged - not used (QoS 0) */
        break;

    default:
        break;
    }
}

/* ── Internal: DNS resolve + connect to broker ──────────────────────── */

static int resolve_broker_addr(void)
{
    struct zsock_addrinfo *ai;
    const struct zsock_addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };

    int err = zsock_getaddrinfo(CONFIG_GLOVE_MQTT_BROKER_HOST,
                                STRINGIFY(CONFIG_GLOVE_MQTT_BROKER_PORT),
                                &hints, &ai);
    if (err != 0) {
        LOG_ERR("DNS resolve '%s' failed: %d",
                CONFIG_GLOVE_MQTT_BROKER_HOST, err);
        return -EIO;
    }

    (void)memcpy(&s_broker_addr, ai->ai_addr, ai->ai_addrlen);
    /* Override port (DNS may not set it from the service string) */
    ((struct sockaddr_in *)&s_broker_addr)->sin_port =
        htons(CONFIG_GLOVE_MQTT_BROKER_PORT);

    zsock_freeaddrinfo(ai);
    return 0;
}

static int do_mqtt_connect(void)
{
    int err = resolve_broker_addr();
    if (err != 0) {
        return err;
    }

    mqtt_client_init(&s_client);

    s_client.broker        = &s_broker_addr;
    s_client.evt_cb        = mqtt_evt_handler;
    s_client.client_id     = (struct mqtt_utf8){
        .utf8 = (const uint8_t *)CLIENT_ID,
        .size = sizeof(CLIENT_ID) - 1U,
    };
    s_client.password      = NULL;
    s_client.user_name     = NULL;
    s_client.protocol_version = MQTT_VERSION_3_1_1;
    s_client.rx_buf        = s_rx_buf;
    s_client.rx_buf_size   = sizeof(s_rx_buf);
    s_client.tx_buf        = s_tx_buf;
    s_client.tx_buf_size   = sizeof(s_tx_buf);

#if defined(CONFIG_GLOVE_MQTT_TLS)
    /* Register CA certificate once (idempotent: ignore -EEXIST). */
    err = tls_credential_add(TLS_SEC_TAG,
                             TLS_CREDENTIAL_CA_CERTIFICATE,
                             s_ca_cert, sizeof(s_ca_cert));
    if ((err != 0) && (err != -EEXIST)) {
        LOG_ERR("TLS CA cert registration failed: %d", err);
        return err;
    }

    static const sec_tag_t sec_tag_list[] = { TLS_SEC_TAG };

    s_client.transport.type = MQTT_TRANSPORT_SECURE;
    s_client.transport.tls.config.peer_verify   = TLS_PEER_VERIFY_REQUIRED;
    s_client.transport.tls.config.cipher_list   = NULL; /* use Zephyr defaults */
    s_client.transport.tls.config.sec_tag_list  = sec_tag_list;
    s_client.transport.tls.config.sec_tag_count = ARRAY_SIZE(sec_tag_list);
    s_client.transport.tls.config.hostname      = CONFIG_GLOVE_MQTT_BROKER_HOST;

    LOG_INF("MQTT: connecting with TLS (tag=%d) to %s:%d",
            TLS_SEC_TAG,
            CONFIG_GLOVE_MQTT_BROKER_HOST,
            CONFIG_GLOVE_MQTT_BROKER_PORT);
#else
    s_client.transport.type = MQTT_TRANSPORT_NON_SECURE;

    LOG_INF("MQTT: connecting (plain TCP) to %s:%d",
            CONFIG_GLOVE_MQTT_BROKER_HOST,
            CONFIG_GLOVE_MQTT_BROKER_PORT);
#endif /* CONFIG_GLOVE_MQTT_TLS */

    err = mqtt_connect(&s_client);
    if (err != 0) {
        LOG_ERR("mqtt_connect failed: %d", err);
    }
    return err;
}

/* ── Internal: publish helper ───────────────────────────────────────── */

static int do_publish(const char *topic, const char *payload_str)
{
    if (!s_mqtt_connected) {
        return -ENOTCONN;
    }

    struct mqtt_publish_param p = {
        .message = {
            .topic = {
                .topic = {
                    .utf8 = (const uint8_t *)topic,
                    .size = strlen(topic),
                },
                .qos = MQTT_QOS_0_AT_MOST_ONCE,
            },
            .payload = {
                .data = (const uint8_t *)payload_str,
                .len  = strlen(payload_str),
            },
        },
        .message_id  = 0U,
        .dup_flag    = 0U,
        .retain_flag = 0U,
    };

    return mqtt_publish(&s_client, &p);
}

/* ── Public API ─────────────────────────────────────────────────────── */

int wifi_mqtt_init(void)
{
    struct net_if *iface = net_if_get_default();
    if (iface == NULL) {
        LOG_ERR("No network interface");
        return -ENODEV;
    }

    net_mgmt_init_event_callback(&s_wifi_cb, wifi_evt_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&s_wifi_cb);

    net_mgmt_init_event_callback(&s_ipv4_cb, ipv4_evt_handler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&s_ipv4_cb);

    struct wifi_connect_req_params params = {
        .ssid        = (const uint8_t *)CONFIG_GLOVE_WIFI_SSID,
        .ssid_length = (uint8_t)strlen(CONFIG_GLOVE_WIFI_SSID),
        .psk         = (const uint8_t *)CONFIG_GLOVE_WIFI_PASSWORD,
        .psk_length  = (uint8_t)strlen(CONFIG_GLOVE_WIFI_PASSWORD),
        .channel     = WIFI_CHANNEL_ANY,
        .security    = WIFI_SECURITY_TYPE_PSK,
    };

    int err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface,
                       &params, sizeof(params));
    if (err != 0) {
        LOG_ERR("WiFi connect request failed: %d", err);
        return err;
    }

    LOG_INF("WiFi: connecting to '%s'...", CONFIG_GLOVE_WIFI_SSID);
    return 0;
}

int wifi_mqtt_publish_gesture(gesture_id_t gesture)
{
    char buf[4];
    (void)snprintf(buf, sizeof(buf), "%u", (unsigned)gesture);
    return do_publish(TOPIC_GESTURE, buf);
}

int wifi_mqtt_publish_adc(const uint16_t adc[4])
{
    char buf[24];
    (void)snprintf(buf, sizeof(buf), "%u,%u,%u,%u",
                   (unsigned)adc[0], (unsigned)adc[1],
                   (unsigned)adc[2], (unsigned)adc[3]);
    return do_publish(TOPIC_ADC, buf);
}

void wifi_mqtt_process(void)
{
    uint32_t now = k_uptime_get_32();

    /* Try to connect to broker once WiFi has an IP */
    if (s_wifi_connected && !s_mqtt_connected) {
        if ((now - s_last_connect_attempt) >= CONNECT_RETRY_MS) {
            s_last_connect_attempt = now;
            (void)do_mqtt_connect();
        }
    }

    if (!s_mqtt_connected) {
        return;
    }

    /* Drive MQTT keepalive */
    int err = mqtt_live(&s_client);
    if ((err != 0) && (err != -EAGAIN)) {
        LOG_WRN("mqtt_live: %d", err);
    }

    /* Drain inbound MQTT data (PUBACK, PINGRESP, etc.)
     * The active socket is in different union members for plain TCP vs TLS. */
#if defined(CONFIG_GLOVE_MQTT_TLS)
    int mqtt_sock = s_client.transport.tls.sock;
#else
    int mqtt_sock = s_client.transport.tcp.sock;
#endif
    struct zsock_pollfd fds = {
        .fd     = mqtt_sock,
        .events = ZSOCK_POLLIN,
    };
    if (zsock_poll(&fds, 1, 0) > 0) {
        err = mqtt_input(&s_client);
        if (err != 0) {
            LOG_WRN("mqtt_input: %d", err);
        }
    }
}

bool wifi_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

#else /* !(CONFIG_WIFI && CONFIG_MQTT_LIB) — stub implementations */

int  wifi_mqtt_init(void)                           { return 0; }
int  wifi_mqtt_publish_gesture(gesture_id_t g)      { ARG_UNUSED(g);   return -ENOTSUP; }
int  wifi_mqtt_publish_adc(const uint16_t adc[4])   { ARG_UNUSED(adc); return -ENOTSUP; }
void wifi_mqtt_process(void)                        {}
bool wifi_mqtt_is_connected(void)                   { return false; }

#endif /* CONFIG_WIFI && CONFIG_MQTT_LIB */
