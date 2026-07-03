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
 *                         Enable: CONFIG_GLOVE_MQTT_TLS=y in prj.conf
 *                         Cert:   certs/broker_ca.pem (embedded at build time)
 *   TLS WebSocket:        port 443, MQTT_TRANSPORT_SECURE_WEBSOCKET
 *                         Enable: CONFIG_GLOVE_MQTT_WEBSOCKET=y
 *
 * Threading: WiFi/MQTT is driven from a dedicated low-priority thread so
 * slow TLS/TCP reconnect attempts cannot stall STM32 UART handling.
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
#include <zephyr/net/sntp.h>
#include <zephyr/version.h>
#if __has_include(<zephyr/sys/clock.h>)
#include <zephyr/sys/clock.h>
#define glove_clock_settime(ts) sys_clock_settime(SYS_CLOCK_REALTIME, (ts))
#else
#include <zephyr/posix/time.h>
#define glove_clock_settime(ts) clock_settime(CLOCK_REALTIME, (ts))
#endif
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>

#ifndef KERNEL_VERSION_MAJOR
#define KERNEL_VERSION_MAJOR 3
#endif

#if KERNEL_VERSION_MAJOR >= 4
#define GLOVE_ZEPHYR4_NET 1
typedef uint64_t glove_net_mgmt_event_t;
#else
#define GLOVE_ZEPHYR4_NET 0
typedef uint32_t glove_net_mgmt_event_t;
#endif

#if defined(CONFIG_GLOVE_MQTT_TLS)
#include <zephyr/net/tls_credentials.h>
#include <mbedtls/x509.h>

/* CA certificates embedded from certs/ at build time.
 * generate_inc_file_for_target() produces a comma-separated byte sequence.
 * PEM parsing in mbedTLS expects the buffer to include a terminating NUL. */
static const uint8_t s_ca_root_cert[] = {
#include "digicert_global_root_g2.pem.inc"
    0x00
};
static const uint8_t s_ca_intermediate_cert[] = {
#include "geotrust_tls_rsa_ca_g1.pem.inc"
    0x00
};
#define TLS_SEC_TAG_ROOT          CONFIG_GLOVE_MQTT_TLS_SEC_TAG
#define TLS_SEC_TAG_INTERMEDIATE  (CONFIG_GLOVE_MQTT_TLS_SEC_TAG + 1)
static const sec_tag_t s_tls_sec_tags[] = {
    TLS_SEC_TAG_ROOT,
    TLS_SEC_TAG_INTERMEDIATE,
};
#endif /* CONFIG_GLOVE_MQTT_TLS */

/* ── Configuration (from Kconfig / prj.conf) ───────────────────────── */

#ifndef CONFIG_GLOVE_MQTT_BROKER_HOST
#define CONFIG_GLOVE_MQTT_BROKER_HOST "io.adafruit.com"
#endif

#ifndef CONFIG_GLOVE_MQTT_BROKER_PORT
#define CONFIG_GLOVE_MQTT_BROKER_PORT 1883
#endif

#define CLIENT_ID_PREFIX "GloveAssist"

/* Adafruit IO topic format: <username>/feeds/<feedname>
 * Generic broker format:    gloveassist/gesture
 * The correct format is selected automatically based on whether
 * CONFIG_GLOVE_MQTT_USERNAME is set. */
#ifdef CONFIG_GLOVE_MQTT_AUTH
#define TOPIC_GESTURE   CONFIG_GLOVE_MQTT_USERNAME "/feeds/glove.gesture"
#define TOPIC_STATUS    CONFIG_GLOVE_MQTT_USERNAME "/feeds/glove.status"
#else
#define TOPIC_GESTURE   "gloveassist/gesture"
#define TOPIC_STATUS    "gloveassist/status"
#endif
#define MQTT_BUF_SIZE        512U
#define MQTT_WS_TMP_BUF_SIZE 1536U
#define MQTT_STATUS_MAX      16U
#define MQTT_QUEUE_DEPTH     8U
#define MQTT_THREAD_STACK    4096U
#define MQTT_THREAD_PRIO     10
#define MQTT_PROCESS_MS      250U
#define CONNECT_RETRY_MS     5000U   /* retry MQTT connect every 5 s */
#define MQTT_CONNACK_TIMEOUT_MS 10000U
#define SNTP_SERVER          "pool.ntp.org"
#define SNTP_TIMEOUT_MS      5000U
#define SNTP_RETRY_MS        30000U
#define MQTT_BROKER_FALLBACK_IP "52.7.84.197"
/* Demo-safe fallback used only when SNTP is blocked by the network.
 * 2026-07-01T00:00:00Z is inside the Adafruit IO certificate validity window. */
#define TLS_FALLBACK_UNIX_TIME 1782864000ULL

enum mqtt_publish_kind {
    MQTT_PUBLISH_GESTURE = 1,
    MQTT_PUBLISH_STATUS,
};

struct mqtt_publish_msg {
    enum mqtt_publish_kind kind;
    gesture_id_t gesture;
    char status[MQTT_STATUS_MAX];
};

static bool config_string_empty(const char *s)
{
    return (s == NULL) || (s[0] == '\0');
}

static bool cloud_credentials_ready(void)
{
    if (config_string_empty(CONFIG_GLOVE_WIFI_SSID)
        || config_string_empty(CONFIG_GLOVE_WIFI_PASSWORD)) {
        LOG_WRN("WiFi/MQTT credentials missing: create application/esp32_app/credentials.conf");
        return false;
    }

#if defined(CONFIG_GLOVE_MQTT_AUTH)
    if (config_string_empty(CONFIG_GLOVE_MQTT_USERNAME)
        || config_string_empty(CONFIG_GLOVE_MQTT_PASSWORD)) {
        LOG_WRN("MQTT auth enabled but username/password are missing");
        return false;
    }
#else
    if (strcmp(CONFIG_GLOVE_MQTT_BROKER_HOST, "io.adafruit.com") == 0) {
        LOG_WRN("Adafruit IO requires MQTT credentials in credentials.conf");
        return false;
    }
#endif

    return true;
}

/* ── MQTT client state ──────────────────────────────────────────────── */

static uint8_t s_rx_buf[MQTT_BUF_SIZE];
static uint8_t s_tx_buf[MQTT_BUF_SIZE];
#if defined(CONFIG_GLOVE_MQTT_WEBSOCKET)
static uint8_t s_ws_tmp_buf[MQTT_WS_TMP_BUF_SIZE];
#endif
static char    s_client_id[sizeof(CLIENT_ID_PREFIX) + 1U + (6U * 2U)];
K_MSGQ_DEFINE(s_publish_q, sizeof(struct mqtt_publish_msg), MQTT_QUEUE_DEPTH, 4);
K_THREAD_STACK_DEFINE(s_mqtt_thread_stack, MQTT_THREAD_STACK);

static struct mqtt_client         s_client;
static struct sockaddr_storage    s_broker_addr;
static bool                       s_broker_addr_resolved;  /* cache DNS across retries */
static bool                       s_mqtt_connected;
static bool                       s_mqtt_connecting;
/* WiFi reconnect exponential backoff: 10s → 20s → 40s → 60s (max).
 * Prevents AP flood-protection from blocking the ESP32 MAC after
 * multiple rapid failed WPA2 association attempts. */
static uint32_t                   s_wifi_backoff_ms   = 10000U;
static uint32_t                   s_wifi_reconnect_after = 0U;
static bool                       s_wifi_connected;
static bool                       s_wifi_connecting;
static uint32_t                   s_last_connect_attempt;
static char                       s_if_name[16];
static bool                       s_wifi_reconnect_needed;
static bool                       s_time_synced;
#if defined(CONFIG_GLOVE_MQTT_TLS)
static bool                       s_tls_preflight_done;
static bool                       s_tls_preflight_ok;
#endif
static struct net_if             *s_wifi_iface;
static struct wifi_connect_req_params s_wifi_params;
static uint32_t                   s_connect_started;
static uint32_t                   s_last_time_sync_attempt;
static struct k_thread            s_mqtt_thread;
static bool                       s_mqtt_thread_started;
static bool                       s_cloud_ready;
static volatile bool              s_cloud_pause_requested;
static uint32_t                   s_cloud_pause_request_ms;
static uint32_t                   s_cloud_pause_until;
/* Set by wifi_mqtt_reconnect_after_ble_pairing() from a BLE callback to cancel
 * the BLE-pairing WiFi-disconnect pause early from the MQTT thread. */
static volatile bool              s_ble_pairing_done;

static void init_client_id(struct net_if *iface)
{
    struct net_linkaddr *ll_addr = NULL;

    if (iface != NULL) {
        ll_addr = net_if_get_link_addr(iface);
    }

    if ((ll_addr != NULL) && (ll_addr->len >= 6U)) {
        const uint8_t *mac = ll_addr->addr;

        (void)snprintf(s_client_id, sizeof(s_client_id),
                       CLIENT_ID_PREFIX "-%02X%02X%02X%02X%02X%02X",
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        (void)snprintf(s_client_id, sizeof(s_client_id), CLIENT_ID_PREFIX);
    }
}

/* ── WiFi / IPv4 event callbacks ────────────────────────────────────── */

static struct net_mgmt_event_callback s_wifi_cb;
static struct net_mgmt_event_callback s_ipv4_cb;

static const char *mqtt_connect_hint(int err)
{
    switch (err) {
    case -ENOENT:
        return "no free NET_MAX_CONN slot, route, or interface";
    case -ENOMEM:
        return "not enough heap/net buffers";
    case -ETIMEDOUT:
        return "TCP connect timeout";
    case -ECONNREFUSED:
        return "broker refused TCP connection";
    case -ECONNABORTED:
        return "TLS/MQTT handshake aborted";
    case -EPROTO:
        return "TLS/MQTT protocol error";
    case -ENETUNREACH:
        return "network unreachable";
    case -EHOSTUNREACH:
        return "broker host unreachable";
    default:
        return "see errno";
    }
}

static int enqueue_publish(const struct mqtt_publish_msg *msg)
{
    int rc;

    if (!s_cloud_ready) {
        return -ENOTSUP;
    }

    rc = k_msgq_put(&s_publish_q, msg, K_NO_WAIT);
    if (rc == 0) {
        return 0;
    }

    struct mqtt_publish_msg dropped;
    (void)k_msgq_get(&s_publish_q, &dropped, K_NO_WAIT);
    rc = k_msgq_put(&s_publish_q, msg, K_NO_WAIT);
    if (rc == 0) {
        LOG_WRN("MQTT publish queue full - dropped oldest event");
    }

    return rc;
}

static void wifi_evt_handler(struct net_mgmt_event_callback *cb,
                             glove_net_mgmt_event_t event,
                             struct net_if *iface)
{
    ARG_UNUSED(iface);

    if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *st = (const struct wifi_status *)cb->info;

        if (st->conn_status == WIFI_STATUS_CONN_SUCCESS) {
            LOG_INF("WiFi associated - waiting for DHCP");
            s_wifi_connecting = false;
            s_wifi_reconnect_needed = false;
            s_wifi_backoff_ms = 10000U;
            s_wifi_reconnect_after = 0U;
        } else {
            LOG_WRN("WiFi connect failed (status %d)", st->conn_status);
            s_wifi_connecting = false;
            s_wifi_reconnect_needed = true;
            s_wifi_reconnect_after = k_uptime_get_32() + s_wifi_backoff_ms;
            s_wifi_backoff_ms = (s_wifi_backoff_ms < 60000U)
                                ? (s_wifi_backoff_ms * 2U) : 60000U;
        }
    } else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        const struct wifi_status *st = (const struct wifi_status *)cb->info;
        if (st != NULL) {
            LOG_WRN("WiFi disconnected (reason %d)", st->disconn_reason);
        } else {
            LOG_INF("WiFi disconnected");
        }
        s_wifi_connecting = false;
        s_wifi_connected = false;
        s_mqtt_connected = false;
        s_mqtt_connecting = false;
        s_connect_started = 0U;
        s_wifi_reconnect_needed = true;
        s_broker_addr_resolved = false;
        s_time_synced = false;
        s_last_time_sync_attempt = 0U;
#if defined(CONFIG_GLOVE_MQTT_TLS)
        s_tls_preflight_done = false;
        s_tls_preflight_ok = false;
#endif
        s_wifi_reconnect_after = k_uptime_get_32() + s_wifi_backoff_ms;
        s_wifi_backoff_ms = (s_wifi_backoff_ms < 60000U)
                            ? (s_wifi_backoff_ms * 2U) : 60000U;  /* re-resolve DNS on next WiFi session */
        ARG_UNUSED(iface);
    }
}

static void ipv4_evt_handler(struct net_mgmt_event_callback *cb,
                             glove_net_mgmt_event_t event,
                             struct net_if *iface)
{
    ARG_UNUSED(cb);

    if (event == NET_EVENT_IPV4_ADDR_ADD) {
        char ip_buf[NET_IPV4_ADDR_LEN];
#if GLOVE_ZEPHYR4_NET
        char gw_buf[NET_IPV4_ADDR_LEN];
        struct net_in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        struct net_in_addr gw = net_if_ipv4_get_gw(iface);

        const char *ip_str = ip ? zsock_inet_ntop(AF_INET, ip, ip_buf, sizeof(ip_buf)) : NULL;
        const char *gw_str = zsock_inet_ntop(AF_INET, &gw, gw_buf, sizeof(gw_buf));

        LOG_INF("IPv4 address acquired - WiFi ready (ip=%s gw=%s)",
                ip_str ? ip_str : "?",
                gw_str ? gw_str : "?");
#else
        struct in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        const char *ip_str = ip ? zsock_inet_ntop(AF_INET, ip, ip_buf, sizeof(ip_buf)) : NULL;

        LOG_INF("IPv4 address acquired - WiFi ready (ip=%s)",
                ip_str ? ip_str : "?");
#endif
        s_wifi_connected = true;
    }
}

/* ── MQTT event handler ─────────────────────────────────────────────── */

#if defined(CONFIG_GLOVE_MQTT_TLS)
static int register_tls_credentials(void)
{
    int err = tls_credential_add(TLS_SEC_TAG_ROOT,
                                 TLS_CREDENTIAL_CA_CERTIFICATE,
                                 s_ca_root_cert, sizeof(s_ca_root_cert));
    if ((err != 0) && (err != -EEXIST)) {
        LOG_ERR("TLS root CA registration failed: %d", err);
        return err;
    }

    err = tls_credential_add(TLS_SEC_TAG_INTERMEDIATE,
                             TLS_CREDENTIAL_CA_CERTIFICATE,
                             s_ca_intermediate_cert,
                             sizeof(s_ca_intermediate_cert));
    if ((err != 0) && (err != -EEXIST)) {
        LOG_ERR("TLS intermediate CA registration failed: %d", err);
        return err;
    }

    return 0;
}

#if GLOVE_ZEPHYR4_NET
static void log_tls_verify_flags(uint32_t flags)
{
    LOG_ERR("TLS X.509 verify flags=0x%08x", flags);

    if ((flags & MBEDTLS_X509_BADCERT_EXPIRED) != 0U) {
        LOG_ERR("TLS cert flag: expired");
    }
    if ((flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0U) {
        LOG_ERR("TLS cert flag: hostname mismatch");
    }
    if ((flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) != 0U) {
        LOG_ERR("TLS cert flag: not trusted by CA chain");
    }
    if ((flags & MBEDTLS_X509_BADCERT_FUTURE) != 0U) {
        LOG_ERR("TLS cert flag: certificate starts in the future");
    }
    if ((flags & MBEDTLS_X509_BADCERT_BAD_MD) != 0U) {
        LOG_ERR("TLS cert flag: unacceptable signature hash");
    }
    if ((flags & MBEDTLS_X509_BADCERT_BAD_PK) != 0U) {
        LOG_ERR("TLS cert flag: unacceptable public-key algorithm");
    }
    if ((flags & MBEDTLS_X509_BADCERT_BAD_KEY) != 0U) {
        LOG_ERR("TLS cert flag: unacceptable public key");
    }
}

static int tls_preflight_check(void)
{
    struct sockaddr *broker = (struct sockaddr *)&s_broker_addr;
    size_t broker_addr_size = sizeof(struct sockaddr_in);
    int peer_verify = ZSOCK_TLS_PEER_VERIFY_OPTIONAL;
    uint32_t verify_result = 0U;
    net_socklen_t optlen = sizeof(verify_result);
    int sock;
    int err;

    if (s_tls_preflight_done) {
        return s_tls_preflight_ok ? 0 : -EACCES;
    }

    sock = zsock_socket(broker->sa_family, NET_SOCK_STREAM, NET_IPPROTO_TLS_1_2);
    if (sock < 0) {
        err = -errno;
        LOG_ERR("TLS preflight socket failed: %d", err);
        return err;
    }

    if (s_if_name[0] != '\0') {
        struct net_ifreq ifname = { 0 };

        strncpy(ifname.ifr_name, s_if_name, sizeof(ifname.ifr_name) - 1U);
        err = zsock_setsockopt(sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_BINDTODEVICE,
                               &ifname, sizeof(ifname));
        if (err < 0) {
            err = -errno;
            LOG_ERR("TLS preflight iface bind failed: %d", err);
            goto out;
        }
    }

    err = zsock_setsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_PEER_VERIFY,
                           &peer_verify, sizeof(peer_verify));
    if (err < 0) {
        err = -errno;
        LOG_ERR("TLS preflight peer_verify set failed: %d", err);
        goto out;
    }

    err = zsock_setsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST,
                           s_tls_sec_tags, sizeof(s_tls_sec_tags));
    if (err < 0) {
        err = -errno;
        LOG_ERR("TLS preflight sec_tag set failed: %d", err);
        goto out;
    }

    err = zsock_setsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_HOSTNAME,
                           CONFIG_GLOVE_MQTT_BROKER_HOST,
                           strlen(CONFIG_GLOVE_MQTT_BROKER_HOST) + 1U);
    if (err < 0) {
        err = -errno;
        LOG_ERR("TLS preflight hostname set failed: %d", err);
        goto out;
    }

    LOG_INF("TLS preflight: checking %s:%d before MQTT auth",
            CONFIG_GLOVE_MQTT_BROKER_HOST,
            CONFIG_GLOVE_MQTT_BROKER_PORT);

    err = zsock_connect(sock, broker, broker_addr_size);
    if (err < 0) {
        err = -errno;
        LOG_ERR("TLS preflight connect failed: %d", err);
        goto out;
    }

    err = zsock_getsockopt(sock, ZSOCK_SOL_TLS, ZSOCK_TLS_CERT_VERIFY_RESULT,
                           &verify_result, &optlen);
    if (err < 0) {
        err = -errno;
        LOG_ERR("TLS preflight verify-result read failed: %d", err);
        goto out;
    }

    if (verify_result == 0U) {
        LOG_INF("TLS preflight: broker certificate verified");
        s_tls_preflight_ok = true;
        err = 0;
    } else {
        log_tls_verify_flags(verify_result);
        s_tls_preflight_ok = false;
        err = -EACCES;
    }

    s_tls_preflight_done = true;

out:
    (void)zsock_close(sock);
    return err;
}
#else
static int tls_preflight_check(void)
{
    s_tls_preflight_done = true;
    s_tls_preflight_ok = true;
    return 0;
}
#endif /* GLOVE_ZEPHYR4_NET */
#endif /* CONFIG_GLOVE_MQTT_TLS */

static void mqtt_evt_handler(struct mqtt_client *client,
                             const struct mqtt_evt *evt)
{
    ARG_UNUSED(client);

    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        s_mqtt_connecting = false;
        s_connect_started = 0U;
        if (evt->result == 0) {
            s_mqtt_connected = true;
            LOG_INF("MQTT connected to %s:%d",
                    CONFIG_GLOVE_MQTT_BROKER_HOST,
                    CONFIG_GLOVE_MQTT_BROKER_PORT);
        } else {
            s_mqtt_connected = false;
            LOG_ERR("MQTT CONNACK error: %d", evt->result);
        }
        break;

    case MQTT_EVT_DISCONNECT:
        s_mqtt_connecting = false;
        s_mqtt_connected = false;
        s_connect_started = 0U;
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
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&s_broker_addr;

        memset(&s_broker_addr, 0, sizeof(s_broker_addr));
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(CONFIG_GLOVE_MQTT_BROKER_PORT);

        if (zsock_inet_pton(AF_INET, MQTT_BROKER_FALLBACK_IP,
                            &addr4->sin_addr) == 1) {
            LOG_WRN("DNS resolve '%s' failed: %d - using fallback IP %s",
                    CONFIG_GLOVE_MQTT_BROKER_HOST, err,
                    MQTT_BROKER_FALLBACK_IP);
            return 0;
        }

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
    int err;

    if (!s_broker_addr_resolved) {
        err = resolve_broker_addr();
        if (err != 0) {
            return err;
        }
        s_broker_addr_resolved = true;

        char ip_buf[NET_IPV4_ADDR_LEN];
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)&s_broker_addr;
        const char *ip_str = zsock_inet_ntop(AF_INET, &addr4->sin_addr,
                                             ip_buf, sizeof(ip_buf));
        LOG_INF("Broker IP cached for session: %s", ip_str ? ip_str : "?");
    }

    mqtt_client_init(&s_client);

    s_client.broker        = &s_broker_addr;
    s_client.evt_cb        = mqtt_evt_handler;
#if GLOVE_ZEPHYR4_NET
    s_client.transport.if_name = (s_if_name[0] != '\0') ? s_if_name : NULL;
#endif
    s_client.client_id     = (struct mqtt_utf8){
        .utf8 = (const uint8_t *)s_client_id,
        .size = strlen(s_client_id),
    };
#ifdef CONFIG_GLOVE_MQTT_AUTH
    static struct mqtt_utf8 s_mqtt_user = {
        .utf8 = (const uint8_t *)CONFIG_GLOVE_MQTT_USERNAME,
        .size = sizeof(CONFIG_GLOVE_MQTT_USERNAME) - 1U,
    };
    static struct mqtt_utf8 s_mqtt_pass = {
        .utf8 = (const uint8_t *)CONFIG_GLOVE_MQTT_PASSWORD,
        .size = sizeof(CONFIG_GLOVE_MQTT_PASSWORD) - 1U,
    };
    s_client.user_name = &s_mqtt_user;
    s_client.password  = &s_mqtt_pass;
#else
    s_client.password      = NULL;
    s_client.user_name     = NULL;
#endif
    s_client.protocol_version = MQTT_VERSION_3_1_1;
    s_client.rx_buf        = s_rx_buf;
    s_client.rx_buf_size   = sizeof(s_rx_buf);
    s_client.tx_buf        = s_tx_buf;
    s_client.tx_buf_size   = sizeof(s_tx_buf);

#if defined(CONFIG_GLOVE_MQTT_TLS)
    /* Register CA certificate once (idempotent: ignore -EEXIST). */
    err = register_tls_credentials();
    if (err != 0) {
        return err;
    }

    err = tls_preflight_check();
    if (err != 0) {
        return err;
    }

    s_client.transport.type = MQTT_TRANSPORT_SECURE;
    s_client.transport.tls.config.peer_verify   = TLS_PEER_VERIFY_REQUIRED;
    s_client.transport.tls.config.cipher_list   = NULL; /* use Zephyr defaults */
    s_client.transport.tls.config.sec_tag_list  = s_tls_sec_tags;
    s_client.transport.tls.config.sec_tag_count = ARRAY_SIZE(s_tls_sec_tags);
    s_client.transport.tls.config.hostname      = CONFIG_GLOVE_MQTT_BROKER_HOST;

#if defined(CONFIG_GLOVE_MQTT_WEBSOCKET)
    s_client.transport.type = MQTT_TRANSPORT_SECURE_WEBSOCKET;
    s_client.transport.websocket.config.host = CONFIG_GLOVE_MQTT_BROKER_HOST;
    s_client.transport.websocket.config.url = "/mqtt";
    s_client.transport.websocket.config.tmp_buf = s_ws_tmp_buf;
    s_client.transport.websocket.config.tmp_buf_len = sizeof(s_ws_tmp_buf);
    s_client.transport.websocket.timeout = MQTT_CONNACK_TIMEOUT_MS;

    LOG_INF("MQTT: connecting with TLS WebSocket (tag=%d) to %s:%d/mqtt",
            TLS_SEC_TAG_ROOT,
            CONFIG_GLOVE_MQTT_BROKER_HOST,
            CONFIG_GLOVE_MQTT_BROKER_PORT);
#else
    LOG_INF("MQTT: connecting with TLS (tags=%d,%d) to %s:%d",
            TLS_SEC_TAG_ROOT,
            TLS_SEC_TAG_INTERMEDIATE,
            CONFIG_GLOVE_MQTT_BROKER_HOST,
            CONFIG_GLOVE_MQTT_BROKER_PORT);
#endif
#else
    s_client.transport.type = MQTT_TRANSPORT_NON_SECURE;

    LOG_INF("MQTT: connecting (plain TCP) to %s:%d",
            CONFIG_GLOVE_MQTT_BROKER_HOST,
            CONFIG_GLOVE_MQTT_BROKER_PORT);
#endif /* CONFIG_GLOVE_MQTT_TLS */

    err = mqtt_connect(&s_client);
    if (err != 0) {
        s_mqtt_connecting = false;
        s_connect_started = 0U;
        LOG_ERR("mqtt_connect failed: %d (%s)", err, mqtt_connect_hint(err));
    } else {
        s_mqtt_connecting = true;
        s_connect_started = k_uptime_get_32();
    }
    return err;
}

static int sync_time_for_tls(void)
{
    struct sntp_time sntp_ts;
    struct timespec tspec;
    int err;

    LOG_INF("SNTP: syncing time for TLS certificate validation...");

    err = sntp_simple(SNTP_SERVER, SNTP_TIMEOUT_MS, &sntp_ts);
    if (err != 0) {
        LOG_WRN("SNTP sync failed: %d - using TLS fallback time", err);
        tspec.tv_sec = (time_t)TLS_FALLBACK_UNIX_TIME;
        tspec.tv_nsec = 0L;
        err = glove_clock_settime(&tspec);
        if (err != 0) {
            LOG_WRN("TLS fallback clock set failed: %d", err);
            return err;
        }

        s_time_synced = true;
        LOG_INF("TLS fallback time set (unix=%" PRIu64 ")",
                (uint64_t)TLS_FALLBACK_UNIX_TIME);
        return 0;
    }

    tspec.tv_sec = (time_t)sntp_ts.seconds;
    tspec.tv_nsec = (long)(((uint64_t)sntp_ts.fraction * NSEC_PER_SEC) >> 32);

    err = glove_clock_settime(&tspec);
    if (err != 0) {
        LOG_WRN("SNTP clock set failed: %d", err);
        return err;
    }

    s_time_synced = true;
    LOG_INF("SNTP: time synced for TLS (unix=%" PRIu64 ")", sntp_ts.seconds);
    return 0;
}

static int do_wifi_connect(void)
{
    if (s_wifi_iface == NULL) {
        s_wifi_iface = net_if_get_default();
    }

    if (s_wifi_iface == NULL) {
        LOG_ERR("No network interface for WiFi reconnect");
        return -ENODEV;
    }

    int err = net_mgmt(NET_REQUEST_WIFI_CONNECT, s_wifi_iface,
                       &s_wifi_params, sizeof(s_wifi_params));
    if (err != 0) {
        LOG_WRN("WiFi reconnect request failed: %d", err);
        s_wifi_connecting = false;
        s_wifi_reconnect_needed = true;
        s_wifi_reconnect_after = k_uptime_get_32() + s_wifi_backoff_ms;
        s_wifi_backoff_ms = (s_wifi_backoff_ms < 60000U)
                            ? (s_wifi_backoff_ms * 2U) : 60000U;
        return err;
    }

    s_wifi_connecting = true;
    s_wifi_reconnect_needed = false;
    LOG_INF("WiFi reconnect requested to '%s'", CONFIG_GLOVE_WIFI_SSID);
    return 0;
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
                .data = (uint8_t *)payload_str,
                .len  = strlen(payload_str),
            },
        },
        .message_id  = 0U,
        .dup_flag    = 0U,
        .retain_flag = 0U,
    };

    int rc = mqtt_publish(&s_client, &p);
    if (rc == 0) {
        LOG_INF("MQTT pub ok: %s <- %s", topic, payload_str);
    } else {
        LOG_WRN("MQTT pub fail: %s rc=%d", topic, rc);
    }

    return rc;
}

static void publish_one_queued_msg(void)
{
    struct mqtt_publish_msg msg;

    if (k_msgq_get(&s_publish_q, &msg, K_NO_WAIT) != 0) {
        return;
    }

    switch (msg.kind) {
    case MQTT_PUBLISH_GESTURE:
        {
            char buf[4];
            (void)snprintf(buf, sizeof(buf), "%u", (unsigned)msg.gesture);
            (void)do_publish(TOPIC_GESTURE, buf);
        }
        break;
    case MQTT_PUBLISH_STATUS:
        (void)do_publish(TOPIC_STATUS, msg.status);
        break;
    default:
        break;
    }
}

static void mqtt_thread_main(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        wifi_mqtt_process();
        k_sleep(K_MSEC(MQTT_PROCESS_MS));
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

int wifi_mqtt_init(void)
{
    struct net_if *iface = net_if_get_default();

    if (!cloud_credentials_ready()) {
        return -ENOTSUP;
    }

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

    s_wifi_iface = iface;
    init_client_id(iface);
    s_cloud_ready = true;

    s_wifi_params = (struct wifi_connect_req_params){
        .ssid        = (const uint8_t *)CONFIG_GLOVE_WIFI_SSID,
        .ssid_length = (uint8_t)strlen(CONFIG_GLOVE_WIFI_SSID),
        .psk         = (const uint8_t *)CONFIG_GLOVE_WIFI_PASSWORD,
        .psk_length  = (uint8_t)strlen(CONFIG_GLOVE_WIFI_PASSWORD),
        .band        = WIFI_FREQ_BAND_2_4_GHZ,
        .channel     = WIFI_CHANNEL_ANY,
        .security    = WIFI_SECURITY_TYPE_PSK,
        .timeout     = 15,
    };

    int if_name_rc = net_if_get_name(iface, s_if_name, sizeof(s_if_name));
    if (if_name_rc <= 0) {
        s_if_name[0] = '\0';
        LOG_WRN("Could not get iface name (rc=%d), MQTT will use default route", if_name_rc);
    } else {
        LOG_INF("MQTT will use default route via iface '%s'", s_if_name);
    }

    if (!s_mqtt_thread_started) {
        s_mqtt_thread_started = true;
        k_thread_create(&s_mqtt_thread,
                        s_mqtt_thread_stack,
                        K_THREAD_STACK_SIZEOF(s_mqtt_thread_stack),
                        mqtt_thread_main,
                        NULL, NULL, NULL,
                        K_PRIO_PREEMPT(MQTT_THREAD_PRIO),
                        0,
                        K_NO_WAIT);
        k_thread_name_set(&s_mqtt_thread, "wifi_mqtt");
    }

    s_wifi_connected = false;
    s_wifi_connecting = false;
    s_wifi_reconnect_needed = true;
    s_wifi_reconnect_after = 0U;
    s_last_connect_attempt = k_uptime_get_32() - CONNECT_RETRY_MS;

    (void)do_wifi_connect();

    return 0;
}

int wifi_mqtt_publish_gesture(gesture_id_t gesture)
{
    const struct mqtt_publish_msg msg = {
        .kind = MQTT_PUBLISH_GESTURE,
        .gesture = gesture,
    };

    return enqueue_publish(&msg);
}

int wifi_mqtt_publish_status(const char *status_str)
{
    if (status_str == NULL) {
        return -EINVAL;
    }

    struct mqtt_publish_msg msg = {
        .kind = MQTT_PUBLISH_STATUS,
    };
    (void)snprintf(msg.status, sizeof(msg.status), "%s", status_str);

    return enqueue_publish(&msg);
}

void wifi_mqtt_process(void)
{
    uint32_t now = k_uptime_get_32();

    if (s_cloud_pause_requested) {
        s_cloud_pause_requested = false;
        s_cloud_pause_until = now + s_cloud_pause_request_ms;

        /*
         * Keep any existing MQTT connection alive during BLE pairing.
         * WiFi stays connected (no disconnect), so the MQTT socket is
         * healthy and gesture publishes go through immediately without
         * waiting for a new TLS handshake after the pairing window.
         * Only NEW connect attempts are suppressed by s_cloud_pause_until.
         */
        LOG_INF("BLE pairing: MQTT new-connects suppressed %u ms (existing conn kept)",
                (unsigned)s_cloud_pause_request_ms);
    }

    /* BLE pairing finished (success or failure): cancel the pause early so
     * WiFi/MQTT can reconnect without waiting for the full 60-second window.
     * MUST be checked BEFORE the early-return below, otherwise the flag is
     * never seen while the pause is active and WiFi waits the full 60 s. */
    if (s_ble_pairing_done) {
        s_ble_pairing_done = false;
        s_cloud_pause_until = 0U;
        s_wifi_reconnect_needed = true;
        s_wifi_reconnect_after = k_uptime_get_32() + 2000U;
        LOG_INF("BLE pairing done \u2014 WiFi/MQTT resuming in 2 s");
    }

    if ((s_cloud_pause_until != 0U)
        && ((int32_t)(now - s_cloud_pause_until) < 0)) {
        return;
    }

    if (s_cloud_pause_until != 0U) {
        s_cloud_pause_until = 0U;
        s_last_connect_attempt = now - CONNECT_RETRY_MS;
        LOG_INF("MQTT/TLS resumed after BLE security window");
    }

    if (!s_wifi_connected && !s_wifi_connecting && s_wifi_reconnect_needed) {
        if ((s_wifi_reconnect_after == 0U)
            || ((int32_t)(now - s_wifi_reconnect_after) >= 0)) {
            (void)do_wifi_connect();
        }
    }

    if (s_mqtt_connecting && (s_connect_started != 0U)
        && ((now - s_connect_started) >= MQTT_CONNACK_TIMEOUT_MS)) {
        LOG_WRN("MQTT CONNACK timeout - aborting socket");
        (void)mqtt_abort(&s_client);
        s_mqtt_connecting = false;
        s_mqtt_connected = false;
        s_connect_started = 0U;
        s_last_connect_attempt = now;
    }

    /* Try to connect to broker once WiFi has an IP */
    if (s_wifi_connected && !s_mqtt_connected && !s_mqtt_connecting) {
        if (!s_time_synced) {
            if ((s_last_time_sync_attempt == 0U)
                || ((now - s_last_time_sync_attempt) >= SNTP_RETRY_MS)) {
                s_last_time_sync_attempt = now;
                (void)sync_time_for_tls();
            }
            return;
        }

        if ((now - s_last_connect_attempt) >= CONNECT_RETRY_MS) {
            s_last_connect_attempt = now;
            (void)do_mqtt_connect();
        }
    }

    /* Always drain input while an MQTT handshake is in progress.
     * CONNACK arrives before s_mqtt_connected becomes true. */
#if defined(CONFIG_GLOVE_MQTT_WEBSOCKET)
    int mqtt_sock = s_client.transport.websocket.sock;
#elif defined(CONFIG_GLOVE_MQTT_TLS)
    int mqtt_sock = s_client.transport.tls.sock;
#else
    int mqtt_sock = s_client.transport.tcp.sock;
#endif
    if ((s_mqtt_connected || s_mqtt_connecting) && (mqtt_sock >= 0)) {
        struct zsock_pollfd fds = {
            .fd     = mqtt_sock,
            .events = ZSOCK_POLLIN | ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL,
        };
        if (zsock_poll(&fds, 1, 0) > 0) {
            if ((fds.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) != 0) {
                LOG_WRN("MQTT socket closed/error revents=0x%x", fds.revents);
                (void)mqtt_abort(&s_client);
                s_mqtt_connecting = false;
                s_mqtt_connected = false;
                s_connect_started = 0U;
            } else if ((fds.revents & ZSOCK_POLLIN) != 0) {
                int in_err = mqtt_input(&s_client);
                if ((in_err != 0) && (in_err != -EAGAIN)) {
                    LOG_WRN("mqtt_input: %d", in_err);
                    s_mqtt_connecting = false;
                    s_mqtt_connected = false;
                    s_connect_started = 0U;
                }
            }
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

    publish_one_queued_msg();
}

bool wifi_mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

void wifi_mqtt_pause_for_ble_security(uint32_t pause_ms)
{
    if (!s_cloud_ready || (pause_ms == 0U)) {
        return;
    }

    s_cloud_pause_request_ms = pause_ms;
    s_cloud_pause_requested = true;
}

void wifi_mqtt_disconnect_for_ble_pairing(void)
{
    if (!s_cloud_ready) {
        return;
    }

    /*
     * Pause MQTT/TLS during BLE SMP pairing to avoid TCP/TLS contention.
     * WiFi stays connected — BT_LONG_WQ_STACK_SIZE=4096 gives P-256 ECC
     * enough stack without needing to free WiFi heap buffers.
     * The pause is cancelled early by wifi_mqtt_reconnect_after_ble_pairing()
     * once security_changed fires, so MQTT reconnects within seconds.
     */
    s_cloud_pause_request_ms = 15000U;
    s_cloud_pause_requested  = true;

    LOG_INF("BLE pairing: MQTT paused 15 s (WiFi stays connected)");
}

void wifi_mqtt_reconnect_after_ble_pairing(void)
{
    if (!s_cloud_ready) {
        return;
    }
    /* Signal the MQTT thread (volatile flag — safe from any callback). */
    s_ble_pairing_done = true;
}

#else /* !(CONFIG_WIFI && CONFIG_MQTT_LIB) — stub implementations */

int  wifi_mqtt_init(void)                                { return 0; }
int  wifi_mqtt_publish_gesture(gesture_id_t g)           { ARG_UNUSED(g);   return -ENOTSUP; }
int  wifi_mqtt_publish_status(const char *status_str)    { ARG_UNUSED(status_str); return -ENOTSUP; }
void wifi_mqtt_process(void)                             {}
bool wifi_mqtt_is_connected(void)                        { return false; }
void wifi_mqtt_pause_for_ble_security(uint32_t pause_ms) { ARG_UNUSED(pause_ms); }
void wifi_mqtt_disconnect_for_ble_pairing(void)          {}
void wifi_mqtt_reconnect_after_ble_pairing(void)         {}

#endif /* CONFIG_WIFI && CONFIG_MQTT_LIB */
