/*
 * uart_esp32.c — UART communication with ESP32 (USART1).
 *
 * TX: interrupt-driven FIFO fill.
 * RX: ISR queues raw bytes; uart_rx_thread parses/authenticates frames and sets:
 *     - s_ack_received: STATUS_ACK or HEARTBEAT (link alive)
 *     - s_cmd_msgq: COMMAND frames queued for main loop
 *
 * Crypto/HMAC stays outside the ISR; command frames are deferred to the main
 * loop via poll_cmd().
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include "frame_protocol.h"
#include "uart_esp32.h"

static const struct device *const s_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

#define UART_RX_BYTE_QUEUE_LEN    256U
#define UART_RX_THREAD_STACK_SIZE 2048U
#define UART_RX_THREAD_PRIORITY   1

/* ── TX state ─────────────────────────────────────────────────────────── */
static volatile uint8_t s_tx_busy;
static uint8_t          s_tx_buf[FRAME_HMAC_SIZE];
static volatile uint8_t s_tx_len;
static volatile uint8_t s_tx_pos;

/* ── RX state ─────────────────────────────────────────────────────────── */
static frame_hmac_parser_t s_parser;
static volatile bool       s_ack_received;  /* STATUS_ACK or HEARTBEAT seen  */
static volatile bool       s_session_ready; /* SESSION_HELLO pending         */
static uint32_t            s_pending_session_nonce;

K_MSGQ_DEFINE(s_rx_byte_msgq, sizeof(uint8_t), UART_RX_BYTE_QUEUE_LEN, 1);
K_MSGQ_DEFINE(s_cmd_msgq, sizeof(frame_t), 8, 4);

/* ── RX diagnostics (ISR-updated, main reads) ────────────────────────── */
static volatile uint8_t s_rx_cmd_count;     /* total COMMAND frames received  */
static volatile uint8_t s_rx_bad_count;     /* frames failing auth/SOF check  */
static volatile uint8_t s_rx_bad_cmd_count; /* bad frames whose type == CMD   */
static volatile bool    s_bad_cmd_seen;     /* instant-poll flag              */

static void uart_rx_thread(void *a, void *b, void *c);
K_THREAD_DEFINE(s_uart_rx_tid, UART_RX_THREAD_STACK_SIZE, uart_rx_thread,
                NULL, NULL, NULL, UART_RX_THREAD_PRIORITY, 0, 0);

static void handle_received_frame(const frame_hmac_t *received)
{
    switch ((frame_type_t)received->base.type) {
    case FRAME_TYPE_STATUS:
        if (received->base.payload[0] == (uint8_t)STATUS_ACK) {
            s_ack_received = true;
        }
        break;
    case FRAME_TYPE_HEARTBEAT:
        s_ack_received = true;
        break;
    case FRAME_TYPE_COMMAND:
        s_rx_cmd_count++;
        if (k_msgq_put(&s_cmd_msgq, &received->base, K_NO_WAIT) != 0) {
            frame_t dropped;

            (void)k_msgq_get(&s_cmd_msgq, &dropped, K_NO_WAIT);
            (void)k_msgq_put(&s_cmd_msgq, &received->base, K_NO_WAIT);
        }
        break;
    case FRAME_TYPE_SESSION:
        if (received->base.payload[0] == (uint8_t)SESSION_HELLO) {
            s_pending_session_nonce = frame_get_counter(&received->base);
            s_session_ready = true;
        }
        break;
    default:
        break;
    }
}

static void uart_rx_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (true) {
        uint8_t byte;
        frame_hmac_t received;

        (void)k_msgq_get(&s_rx_byte_msgq, &byte, K_FOREVER);

        int rc = frame_hmac_parser_push_byte(&s_parser, byte, &received);
        if (rc == -2) {
            s_rx_bad_count++; /* HMAC/counter/SOF failure */
            if (received.base.type == (uint8_t)FRAME_TYPE_COMMAND) {
                s_rx_bad_cmd_count++;
                s_bad_cmd_seen = true;
            }
        } else if (rc == 1) {
            handle_received_frame(&received);
        }
    }
}

/* ── ISR ──────────────────────────────────────────────────────────────── */
static void uart_isr(const struct device *dev, void *user_data)
{
    (void)user_data;

    if (!uart_irq_update(dev)) {
        return;
    }

    /* ── RX ────────────────────────────────────────────────────────── */
    if (uart_irq_rx_ready(dev)) {
        uint8_t bytes[16];
        int rx;

        while ((rx = uart_fifo_read(dev, bytes, (int)sizeof(bytes))) > 0) {
            for (int i = 0; i < rx; i++) {
                if (k_msgq_put(&s_rx_byte_msgq, &bytes[i], K_NO_WAIT) != 0) {
                    uint8_t dropped;

                    s_rx_bad_count++;
                    (void)k_msgq_get(&s_rx_byte_msgq, &dropped, K_NO_WAIT);
                    (void)k_msgq_put(&s_rx_byte_msgq, &bytes[i], K_NO_WAIT);
                }
            }
        }
    }

    /* ── TX ────────────────────────────────────────────────────────── */
    if (uart_irq_tx_ready(dev)) {
        while ((s_tx_busy != 0U) && (s_tx_pos < s_tx_len)) {
            int sent = uart_fifo_fill(dev, &s_tx_buf[s_tx_pos],
                                      (int)(s_tx_len - s_tx_pos));
            if (sent <= 0) {
                break;
            }
            s_tx_pos = (uint8_t)(s_tx_pos + (uint8_t)sent);
        }
        if ((s_tx_busy != 0U) && (s_tx_pos >= s_tx_len)) {
            uart_irq_tx_disable(dev);
            s_tx_busy = 0U;
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

void uart_esp32_init(void)
{
    if (!device_is_ready(s_uart)) {
        return;
    }
    k_msgq_purge(&s_rx_byte_msgq);
    frame_hmac_parser_init(&s_parser);
    uart_irq_callback_user_data_set(s_uart, uart_isr, NULL);
    uart_irq_rx_enable(s_uart);
}

void uart_esp32_send_hmac_frame(const frame_hmac_t *frame)
{
    if (frame == NULL) {
        return;
    }
    if (s_tx_busy != 0U) {
        return; /* drop: TX already in progress */
    }
    (void)memcpy(s_tx_buf, frame, FRAME_HMAC_SIZE);
    s_tx_len  = (uint8_t)FRAME_HMAC_SIZE;
    s_tx_pos  = 0U;
    s_tx_busy = 1U;
    uart_irq_tx_enable(s_uart);
}

void uart_esp32_send_session_ack(uint32_t session_nonce)
{
    static uint8_t session_seq;
    frame_hmac_t frame;

    if (frame_build_session(&frame, session_seq++,
                            (uint8_t)SESSION_ACK,
                            session_nonce) == 0) {
        uart_esp32_send_hmac_frame(&frame);
    }
}

bool uart_esp32_poll_session_hello(uint32_t *session_nonce)
{
    unsigned int key = irq_lock();
    bool ready = s_session_ready;

    if (ready) {
        if (session_nonce != NULL) {
            *session_nonce = s_pending_session_nonce;
        }
        s_session_ready = false;
    }

    irq_unlock(key);
    return ready;
}

bool uart_esp32_poll_ack(void)
{
    if (!s_ack_received) {
        return false;
    }
    s_ack_received = false;
    return true;
}

bool uart_esp32_poll_cmd(frame_t *out)
{
    if (out == NULL) {
        return false;
    }

    return (k_msgq_get(&s_cmd_msgq, out, K_NO_WAIT) == 0);
}

void uart_esp32_get_rx_diag(uint8_t *cmds, uint8_t *bad_frames,
                             uint8_t *bad_cmd_frames)
{
    unsigned int key = irq_lock();
    *cmds           = s_rx_cmd_count;       s_rx_cmd_count     = 0U;
    *bad_frames     = s_rx_bad_count;       s_rx_bad_count     = 0U;
    *bad_cmd_frames = s_rx_bad_cmd_count;   s_rx_bad_cmd_count = 0U;
    irq_unlock(key);
}

bool uart_esp32_poll_bad_cmd(void)
{
    unsigned int key = irq_lock();
    bool seen = s_bad_cmd_seen;
    s_bad_cmd_seen = false;
    irq_unlock(key);
    return seen;
}
