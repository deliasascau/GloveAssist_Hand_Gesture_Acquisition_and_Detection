/*
 * uart_esp32.c — ISR-driven UART communication with ESP32 (USART1).
 *
 * TX: interrupt-driven FIFO fill.
 * RX: ISR collects bytes via frame_parser and sets one of two flags:
 *     - s_ack_received: STATUS_ACK or HEARTBEAT (link alive)
 *     - s_cmd_ready / s_pending_cmd: COMMAND frame for main loop
 *
 * The main loop must never call k_msleep while processing inside the ISR,
 * so command frames are deferred to the main loop via poll_cmd().
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include "frame_protocol.h"
#include "uart_esp32.h"

static const struct device *const s_uart = DEVICE_DT_GET(DT_NODELABEL(usart1));

/* ── TX state ─────────────────────────────────────────────────────────── */
static volatile uint8_t s_tx_busy;
static uint8_t          s_tx_buf[FRAME_SIZE];
static volatile uint8_t s_tx_len;
static volatile uint8_t s_tx_pos;

/* ── RX state ─────────────────────────────────────────────────────────── */
static frame_parser_t  s_parser;
static volatile bool   s_ack_received;  /* STATUS_ACK or HEARTBEAT seen    */
static volatile bool   s_cmd_ready;     /* COMMAND frame pending for main   */
static frame_t         s_pending_cmd;   /* guarded by irq_lock in poll_cmd  */

/* ── RX diagnostics (ISR-updated, main reads) ────────────────────────── */
static volatile uint8_t s_rx_cmd_count;     /* total COMMAND frames received  */
static volatile uint8_t s_rx_bad_count;     /* frames failing CRC/SOF check   */
static volatile uint8_t s_rx_bad_cmd_count; /* bad frames whose type == CMD   */
static volatile bool    s_bad_cmd_seen;     /* instant-poll flag              */

/* ── ISR ──────────────────────────────────────────────────────────────── */
static void uart_isr(const struct device *dev, void *user_data)
{
    (void)user_data;

    if (!uart_irq_update(dev)) {
        return;
    }

    /* ── RX ────────────────────────────────────────────────────────── */
    if (uart_irq_rx_ready(dev)) {
        uint8_t byte;
        while (uart_fifo_read(dev, &byte, 1) == 1) {
            frame_t received;
            int rc = frame_parser_push_byte(&s_parser, byte, &received);
            if (rc == -2) {
                s_rx_bad_count++; /* CRC/SOF failure */
                if (received.type == (uint8_t)FRAME_TYPE_COMMAND) {
                    s_rx_bad_cmd_count++;
                    s_bad_cmd_seen = true;
                }
            } else if (rc == 1) {
                switch ((frame_type_t)received.type) {
                case FRAME_TYPE_STATUS:
                    if (received.payload[0] == (uint8_t)STATUS_ACK) {
                        s_ack_received = true;
                    }
                    break;
                case FRAME_TYPE_HEARTBEAT:
                    s_ack_received = true;
                    break;
                case FRAME_TYPE_COMMAND:
                    s_rx_cmd_count++; /* count every CMD frame received  */
                    /* Only store one CMD at a time; main loop must drain fast. */
                    if (!s_cmd_ready) {
                        s_pending_cmd = received;
                        s_cmd_ready   = true;
                    }
                    break;
                default:
                    break;
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
    frame_parser_init(&s_parser);
    uart_irq_callback_user_data_set(s_uart, uart_isr, NULL);
    uart_irq_rx_enable(s_uart);
}

void uart_esp32_send_frame(const frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    if (s_tx_busy != 0U) {
        return; /* drop: TX already in progress */
    }
    (void)memcpy(s_tx_buf, frame, FRAME_SIZE);
    s_tx_len  = (uint8_t)FRAME_SIZE;
    s_tx_pos  = 0U;
    s_tx_busy = 1U;
    uart_irq_tx_enable(s_uart);
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
    unsigned int key = irq_lock();
    bool ready = s_cmd_ready;
    if (ready) {
        *out        = s_pending_cmd;
        s_cmd_ready = false;
    }
    irq_unlock(key);
    return ready;
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
