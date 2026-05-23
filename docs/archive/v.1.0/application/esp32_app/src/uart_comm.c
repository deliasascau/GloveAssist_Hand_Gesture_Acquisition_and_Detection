/**
 * @file uart_comm.c
 * @brief UART inter-MCU communication (ESP32 side)
 *
 * Protocol: glove_frame_t framing (PROTO_FRAME_SIZE = 12 bytes) —
 *   [SOF 0xAA 1B][TYPE 1B][SEQ 1B][PAYLOAD 8B XOR'd][CRC8 1B]
 *
 * UART2: GPIO16 RX (← STM32 PA9 TX), GPIO17 TX (→ STM32 PA10 RX)
 * Baud:  115200, 8N1 — prototype-safe rate for glove wiring
 *
 * RX: ISR-driven byte accumulator → ring buffer → thread processes frames
 * TX: protected by mutex, blocking poll_out per byte
 *
 * Heartbeat: k_timer fires every HEARTBEAT_INTERVAL_MS,
 *            sends MSG_TYPE_HEARTBEAT frame to STM32 via uart_comm_send().
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

#include "app_config.h"
#include "common_types.h"
#include "frame_protocol.h"
#include "comms_ble.h"
#include "uart_comm.h"
#include "wifi_mqtt.h"
#include "sensor_filter.h"
#include "gesture_classify.h"

LOG_MODULE_REGISTER(uart_comm, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- UART device ---------- */
static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart1));

/* ---------- RX ring buffer (2× frame size for safety) ---------- */
#define RX_BUF_SIZE (sizeof(glove_frame_t) * 4U)
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_head; /* write index (ISR) */
static uint32_t rx_tail; /* read  index (thread) */
static volatile uint32_t g_rx_bytes;

/* Semaphore: ISR signals thread that at least one byte arrived */
K_SEM_DEFINE(rx_data_sem, 0, 1);

/* TX spinlock — safe to use from timer ISR context */
static struct k_spinlock tx_lock;

/* BLE TX queue defined in main.c */
extern struct k_msgq ble_tx_msgq;

typedef struct {
    uint32_t tx_frames;
    uint32_t rx_frames_ok;
    uint32_t rx_crc_drop;
    uint32_t rx_replay_drop;
    uint32_t rx_honeypot;
    uint32_t lockdown_entries;
    uint8_t  last_rx_type;
    uint8_t  last_rx_seq;
    bool     lockdown_active;
} sniff_stats_t;

static sniff_stats_t g_sniff_stats;
static uint8_t g_invalid_streak;
static bool g_seq_seen;
static uint8_t g_last_seq;
#if defined(CONFIG_NETWORKING) && defined(CONFIG_MQTT_LIB)
static uint32_t g_last_status_publish_ms;
#endif

static void security_trigger_lockdown(const char *reason)
{
    if (g_sniff_stats.lockdown_active) {
        return;
    }

    g_sniff_stats.lockdown_active = true;
    g_sniff_stats.lockdown_entries++;
    LOG_ERR("SECURITY LOCKDOWN: %s", reason);

    const char *alert = "SECURITY_LOCKDOWN\n";
    (void)ble_send_notification((const uint8_t *)alert, (uint16_t)strlen(alert));
}

static void security_note_invalid(const char *reason)
{
    g_invalid_streak++;
    LOG_WRN("Security invalid #%u/%u: %s",
            g_invalid_streak, LOCKDOWN_INVALID_FRAMES, reason);

    if (g_invalid_streak >= LOCKDOWN_INVALID_FRAMES) {
        security_trigger_lockdown(reason);
    }
}

static bool security_accept_seq(uint8_t seq)
{
    if (!g_seq_seen) {
        g_seq_seen = true;
        g_last_seq = seq;
        return true;
    }

    uint8_t delta = (uint8_t)(seq - g_last_seq);
    if ((delta == 0U) || (delta > (uint8_t)COUNTER_MAX_GAP)) {
        g_sniff_stats.rx_replay_drop++;
        security_note_invalid("replay/counter anomaly");
        return false;
    }

    g_last_seq = seq;
    return true;
}

int uart_comm_send_sniff_report(void)
{
    char line[140];
    int n = snprintf(line, sizeof(line),
                     "SNIFF tx=%u rx=%u crc_drop=%u replay_drop=%u hp=%u lock=%u\n",
                     g_sniff_stats.tx_frames,
                     g_sniff_stats.rx_frames_ok,
                     g_sniff_stats.rx_crc_drop,
                     g_sniff_stats.rx_replay_drop,
                     g_sniff_stats.rx_honeypot,
                     g_sniff_stats.lockdown_active ? 1U : 0U);

    if (n <= 0) {
        return -EINVAL;
    }

    return ble_send_notification((const uint8_t *)line, (uint16_t)n);
}

static const char *gesture_name(uint8_t gesture_id)
{
    static const char *const names[] = {
        [GESTURE_NONE]   = "NONE",
        [GESTURE_INDEX]  = "INDEX",
        [GESTURE_MIDDLE] = "MIDDLE",
        [GESTURE_RING]   = "RING",
        [GESTURE_PINKY]  = "PINKY",
        [GESTURE_FIST]   = "FIST",
        [GESTURE_HELP]   = "HELP",
    };

    return (gesture_id <= GESTURE_MAX_ID) ? names[gesture_id] : "UNKNOWN";
}

static void ble_send_gesture_text(const frame_sensor_payload_t *pkt)
{
    char line[96];
    int n = snprintf(line, sizeof(line),
                     "GESTURE id=%u name=%s conf=%u flex=%u,%u,%u,%u\n",
                     pkt->gesture_id, gesture_name(pkt->gesture_id),
                     pkt->confidence,
                     pkt->flex[0], pkt->flex[1], pkt->flex[2], pkt->flex[3]);

    if (n > 0) {
        (void)ble_send_notification((const uint8_t *)line, (uint16_t)n);
    }
}

/* ---------- UART ISR: accumulate bytes into ring buffer ---------- */
static void uart_rx_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t byte;
        int n = uart_fifo_read(dev, &byte, 1);
        if (n <= 0) {
            break;
        }
        uint32_t next = (rx_head + 1U) % RX_BUF_SIZE;
        if (next != rx_tail) { /* drop if full */
            rx_buf[rx_head] = byte;
            rx_head = next;
        }
        g_rx_bytes++;
        k_sem_give(&rx_data_sem);
    }
}

/* ---------- TX: send raw bytes ---------- */
int uart_comm_send(const glove_frame_t *frame)
{
    if (frame == NULL) {
        return -EINVAL;
    }

    k_spinlock_key_t key = k_spin_lock(&tx_lock);
    const uint8_t *p = (const uint8_t *)frame;
    for (size_t i = 0U; i < sizeof(glove_frame_t); i++) {
        uart_poll_out(uart_dev, p[i]);
    }
    k_spin_unlock(&tx_lock, key);

    g_sniff_stats.tx_frames++;
    return 0;
}

/* ---------- Command relay: BLE Central -> STM32 ---------- */
int uart_comm_send_command(const frame_command_payload_t *cmd)
{
    if (cmd == NULL) {
        return -EINVAL;
    }

    /* Validare cmd_id — rejecta comenzi necunoscute inainte de a trimite */
    if ((cmd->cmd_id < CMD_CALIBRATE || cmd->cmd_id > CMD_RESET) &&
        (cmd->cmd_id != CMD_ACK_RECEIVED)) {
        LOG_WRN("Comanda BLE necunoscuta: 0x%02X — ignorata", cmd->cmd_id);
        return -EINVAL;
    }

    glove_frame_t frame;
    int32_t rc = frame_build(&frame, MSG_TYPE_COMMAND,
                                 (const uint8_t *)cmd,
                                 (uint8_t)sizeof(frame_command_payload_t));
    if (rc < 0) {
        LOG_ERR("frame_build CMD failed: %d", (int)rc);
        return (int)rc;
    }

    LOG_INF("BLE CMD relay → STM32: cmd_id=0x%02X finger=%u value=%u",
            cmd->cmd_id, cmd->finger_idx, cmd->value);
    return uart_comm_send(&frame);
}

/* ---------- Heartbeat timer ---------- */
static void heartbeat_timer_fn(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    frame_heartbeat_payload_t hb;
    (void)memset(&hb, 0, sizeof(hb));
    hb.status        = 0x01U; /* bit 0: ESP32 alive */
    hb.ble_connected = ble_is_connected() ? 1U : 0U;
    hb.rssi          = ble_get_rssi();

#if defined(CONFIG_NETWORKING) && defined(CONFIG_MQTT_LIB)
    uint32_t now_ms = k_uptime_get_32();
    if ((now_ms - g_last_status_publish_ms) >= STATUS_PUBLISH_INTERVAL_MS) {
        (void)wifi_mqtt_publish_status(true, ble_is_connected());
        g_last_status_publish_ms = now_ms;
    }
#endif

    glove_frame_t frame;
    (void)frame_build(&frame, MSG_TYPE_HEARTBEAT,
                          (const uint8_t *)&hb, (uint8_t)sizeof(hb));
    (void)uart_comm_send(&frame);
}

K_TIMER_DEFINE(heartbeat_timer, heartbeat_timer_fn, NULL);

/* ---------- RX thread: assemble frames from ring buffer ---------- */
void uart_comm_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART2 not ready — UART comm thread exiting");
        return;
    }

    /* CONNECTIVITY TEST MODE: pure polling, no ISR
     * Enables after hardware is confirmed working */
    /* uart_irq_callback_set(uart_dev, uart_rx_isr); */
    /* uart_irq_rx_enable(uart_dev); */

    k_timer_start(&heartbeat_timer,
                  K_MSEC(HEARTBEAT_INTERVAL_MS),
                  K_MSEC(HEARTBEAT_INTERVAL_MS));

    LOG_INF("UART comm thread started (115200 baud, heartbeat %u ms)",
            HEARTBEAT_INTERVAL_MS);

    uint8_t     assemble[sizeof(glove_frame_t)];
    uint32_t    assemble_pos = 0U;
    glove_frame_t rx_frame;
    uint32_t    last_stats_ms = k_uptime_get_32();

    while (1) {
        /* CONNECTIVITY TEST: pure polling, drain all available bytes */
        unsigned char poll_byte;
        while (uart_poll_in(uart_dev, &poll_byte) == 0) {
            uint32_t next = (rx_head + 1U) % RX_BUF_SIZE;
            if (next != rx_tail) {
                rx_buf[rx_head] = (uint8_t)poll_byte;
                rx_head = next;
            }
            g_rx_bytes++;
        }
        k_usleep(500U);  /* 0.5ms between polls = catches 115200/10 = 11520 bytes/s */

        uint32_t now_stats_ms = k_uptime_get_32();
        if ((now_stats_ms - last_stats_ms) >= 5000U) {
            LOG_INF("UART stats: bytes=%u ok=%u crc_drop=%u replay_drop=%u last_type=0x%02x last_seq=%u",
                    g_rx_bytes,
                    g_sniff_stats.rx_frames_ok,
                    g_sniff_stats.rx_crc_drop,
                    g_sniff_stats.rx_replay_drop,
                    g_sniff_stats.last_rx_type,
                    g_sniff_stats.last_rx_seq);
            last_stats_ms = now_stats_ms;
        }

        /* Drain all available bytes from ring buffer */
        while (rx_tail != rx_head) {
            uint8_t byte = rx_buf[rx_tail];
            rx_tail = (rx_tail + 1U) % RX_BUF_SIZE;

            /* Hunt for SOF byte to (re)sync */
            if (assemble_pos == 0U && byte != PROTO_SOF) {
                continue;
            }

            assemble[assemble_pos++] = byte;

            if (assemble_pos < sizeof(glove_frame_t)) {
                continue; /* need more bytes */
            }

            /* Full frame accumulated — copy and validate */
            assemble_pos = 0U;
            (void)memcpy(&rx_frame, assemble, sizeof(glove_frame_t));

            /* DIAGNOSTIC: Hex dump first 10 frames */
            static uint32_t frame_count = 0;
            if (frame_count < 10) {
                LOG_INF("RX Frame #%u: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                        frame_count,
                        assemble[0], assemble[1], assemble[2], assemble[3],
                        assemble[4], assemble[5], assemble[6], assemble[7],
                        assemble[8], assemble[9], assemble[10], assemble[11]);
                frame_count++;
            }

            if (frame_validate(&rx_frame) != 0) {
                g_sniff_stats.rx_crc_drop++;
                security_note_invalid("crc mismatch");
                LOG_DBG("UART RX: invalid CRC — discarding frame");
                continue;
            }

            if (!security_accept_seq(rx_frame.seq)) {
                continue;
            }

            g_invalid_streak = 0U;
            g_sniff_stats.rx_frames_ok++;
            g_sniff_stats.last_rx_type = rx_frame.type;
            g_sniff_stats.last_rx_seq = rx_frame.seq;

            if (g_sniff_stats.lockdown_active && (rx_frame.type != MSG_TYPE_HONEYPOT)) {
                LOG_WRN("SECURITY_LOCKDOWN active — frame dropped type=0x%02X",
                        rx_frame.type);
                continue;
            }

            /* Dispatch */
            switch (rx_frame.type) {
            case MSG_TYPE_GESTURE: {
                uint8_t plain[PROTO_PAYLOAD_SIZE];
                frame_sensor_payload_t gesture;

                frame_decode_payload(&rx_frame, plain);
                (void)memcpy(&gesture, plain, sizeof(gesture));

                LOG_INF("UART RX gesture from STM32: id=%u conf=%u",
                        gesture.gesture_id, gesture.confidence);
                ble_send_gesture_text(&gesture);

                /* BLE delivery confirmation — phone shows receipt */
                {
                    static const char *const k_gname[] = {
                        [GESTURE_NONE]   = "NONE",
                        [GESTURE_INDEX]  = "INDEX",
                        [GESTURE_MIDDLE] = "MIDDLE",
                        [GESTURE_RING]   = "RING",
                        [GESTURE_PINKY]  = "PINKY",
                        [GESTURE_FIST]   = "FIST",
                        [GESTURE_HELP]   = "HELP",
                    };
                    const char *gname = (gesture.gesture_id <= GESTURE_MAX_ID) ?
                                        k_gname[gesture.gesture_id] : "UNK";
                    char confirm[48];
                    int cn = snprintf(confirm, sizeof(confirm),
                                     "OK:BLE %s conf=%u\n",
                                     gname, gesture.confidence);
                    if (cn > 0) {
                        (void)ble_send_notification((const uint8_t *)confirm,
                                                    (uint16_t)cn);
                    }
                }

                /* Forward new non-idle gesture events to cloud.
                 * Repeat frames stay local/BLE-only for app resync.
                 */
        #if defined(CONFIG_NETWORKING) && defined(CONFIG_MQTT_LIB)
                if ((gesture.gesture_id != GESTURE_NONE) &&
                    ((gesture.status_flags & SENSOR_STATUS_NEW_EVENT) != 0U)) {
                    int mq_rc = wifi_mqtt_publish_gesture(gesture.gesture_id,
                                                          gesture.confidence);
                    if (mq_rc == 0) {
                        const char mqtt_ack[] = "OK:MQTT queued\n";
                        (void)ble_send_notification((const uint8_t *)mqtt_ack,
                                                    (uint16_t)(sizeof(mqtt_ack) - 1U));
                    }
                }
        #endif
                break;
            }

            case MSG_TYPE_SENSOR_DATA: {
                uint8_t plain[PROTO_PAYLOAD_SIZE];

                frame_decode_payload(&rx_frame, plain);
                LOG_DBG("UART RX sensor data from STM32");
                (void)ble_send_notification(plain,
                                            (uint16_t)PROTO_PAYLOAD_SIZE);
                break;
            }

            case MSG_TYPE_RAW_ADC: {
                /* NEW ARCHITECTURE (v2.0): ESP32 is the BRAIN
                 * 1. Receive raw 12-bit ADC from STM32
                 * 2. Apply moving average filter
                 * 3. Classify gesture
                 * 4. Send GESTURE frame back to STM32 for haptic feedback
                 * 5. Forward gesture to BLE/MQTT
                 */
                uint8_t plain[PROTO_PAYLOAD_SIZE];
                frame_raw_adc_payload_t raw;
                
                frame_decode_payload(&rx_frame, plain);
                (void)memcpy(&raw, plain, sizeof(raw));
                
                /* Apply filter to each channel */
                uint16_t filtered[NUM_FLEX_SENSORS];
                for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
                    filtered[ch] = sensor_filter_update(ch, raw.raw[ch]);
                }
                
                /* Classify gesture */
                static uint8_t last_gesture = GESTURE_NONE;
                static int64_t gesture_start_ms = 0;
                uint8_t current_gesture = gesture_classify(filtered);
                
                /* Debounce: require 2s hold time */
                int64_t now_ms = k_uptime_get();
                if (current_gesture != last_gesture) {
                    last_gesture = current_gesture;
                    gesture_start_ms = now_ms;
                } else if (current_gesture != GESTURE_NONE &&
                           (now_ms - gesture_start_ms) >= 2000) {
                    /* Gesture stable for 2s → send to STM32 + BLE */
                    frame_gesture_payload_t gest_frame;
                    (void)memset(&gest_frame, 0, sizeof(gest_frame));
                    gest_frame.gesture_id = current_gesture;
                    gest_frame.confidence = 95; /* TODO: calculate real confidence */
                    uint16_t hold_time = (uint16_t)(now_ms - gesture_start_ms);
                    gest_frame.hold_time_ms_h = (uint8_t)(hold_time >> 8);
                    gest_frame.hold_time_ms_l = (uint8_t)(hold_time & 0xFF);
                    
                    /* Send to STM32 for haptic feedback */
                    glove_frame_t tx_frame;
                    int32_t rc = frame_build(&tx_frame, MSG_TYPE_GESTURE,
                                             (const uint8_t *)&gest_frame,
                                             sizeof(gest_frame));
                    if (rc >= 0) {
                        (void)uart_comm_send(&tx_frame);
                        LOG_INF("Gesture detected: %s (conf=%u, hold=%ums)",
                                gesture_name(current_gesture), 
                                gest_frame.confidence, hold_time);
                    }
                    
                    /* Send to BLE */
                    char line[96];
                    int n = snprintf(line, sizeof(line),
                                     "GESTURE id=%u name=%s conf=%u flex=%u,%u,%u,%u\n",
                                     current_gesture, gesture_name(current_gesture),
                                     gest_frame.confidence,
                                     filtered[0], filtered[1], filtered[2], filtered[3]);
                    if (n > 0) {
                        (void)ble_send_notification((const uint8_t *)line, (uint16_t)n);
                    }
                    
                    /* Reset to avoid repeat */
                    gesture_start_ms = now_ms;
                }
                
                /* Debug: log raw ADC periodically */
                static uint32_t log_counter = 0;
                if ((log_counter++ % 50) == 0) { /* Every 1s @ 50Hz */
                    LOG_DBG("RAW: %u,%u,%u,%u → FILT: %u,%u,%u,%u → GEST: %s",
                            raw.raw[0], raw.raw[1], raw.raw[2], raw.raw[3],
                            filtered[0], filtered[1], filtered[2], filtered[3],
                            gesture_name(current_gesture));
                }
                break;
            }

            case MSG_TYPE_POLL:
                /* STM32 polls — heartbeat timer takes care of reply */
                break;

            case MSG_TYPE_HONEYPOT:
                g_sniff_stats.rx_honeypot++;
                break;

            default:
                LOG_DBG("UART RX unknown type=0x%02X", rx_frame.type);
                break;
            }
        }
    }
}
