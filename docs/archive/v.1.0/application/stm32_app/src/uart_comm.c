/**
 * @file uart_comm.c
 * @brief UART inter-MCU communication (STM32 side)
 *
 * Protocol: glove_frame_t framing (PROTO_FRAME_SIZE = 12 bytes) —
 *   [SOF 0xAA 1B][TYPE 1B][SEQ 1B][PAYLOAD 8B XOR'd][CRC8 1B]
 *
 * USART1: PA9 TX (→ ESP32 GPIO16 RX), PA10 RX (← ESP32 GPIO17 TX)
 * Baud:   115200, 8N1
 *
 * TX thread: wakes every FRAME_POLL_INTERVAL_MS (100 ms), builds & sends frame
 * RX:        ISR-driven ring buffer → thread processes incoming heartbeats
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "frame_protocol.h"
#include "safety_diag.h"
#include "uart_comm.h"
#include "calibration.h"
#include "haptic_ui.h"
#include "security.h"

LOG_MODULE_REGISTER(uart_comm, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- UART device ---------- */
static const struct device *const uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

/* ---------- Diagnostic LED (PC13 built-in) ---------- */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* ---------- RX ring buffer ---------- */
#define RX_BUF_SIZE (sizeof(glove_frame_t) * 4U)
static uint8_t  rx_buf[RX_BUF_SIZE];
static uint32_t rx_head; /* write (ISR)   */
static uint32_t rx_tail; /* read  (thread) */

K_SEM_DEFINE(rx_data_sem, 0, 1);

/* TX mutex */
static struct k_mutex tx_mutex;

/* Sensor queue defined in main.c */
extern struct k_msgq sensor_msgq;

/* ---------- UART ISR ---------- */
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
        if (next != rx_tail) {
            rx_buf[rx_head] = byte;
            rx_head = next;
        }
        k_sem_give(&rx_data_sem);
    }
}

/* ---------- TX helper ---------- */
static void uart_send_frame(const glove_frame_t *frame)
{
    k_mutex_lock(&tx_mutex, K_FOREVER);
    const uint8_t *p = (const uint8_t *)frame;
    for (size_t i = 0U; i < sizeof(glove_frame_t); i++) {
        uart_poll_out(uart_dev, p[i]);
    }
    k_mutex_unlock(&tx_mutex);
}

/* ---------- UART comm thread ---------- */
void uart_comm_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    if (!device_is_ready(uart_dev)) {
        printk("FATAL: USART1 NOT READY\n");
        return;
    }

    /* Init LED */
    if (device_is_ready(led.port)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    }

    /* --- CONNECTIVITY TEST: pure uart_poll_out, no ISR, no irq_enable --- */
    /* Eliminates any IRQ conflict. If ESP32 still sees 0 bytes => hardware. */
    uint32_t count = 0;
    while (1) {
        uart_poll_out(uart_dev, 0x55U);
        count++;
        if ((count % 500U) == 0U) {      /* every 500ms */
            if (device_is_ready(led.port)) {
                gpio_pin_toggle_dt(&led);
            }
        }
        k_usleep(10000U);  /* 10ms = 100 bytes/s */
    }
}

/* ---------- NEW (v2.0): Send RAW_ADC directly from sensor thread ---------- */
int uart_comm_send_raw_adc(const frame_raw_adc_payload_t *payload)
{
    if (payload == NULL) {
        return -EINVAL;
    }

    glove_frame_t frame;
    int32_t rc = frame_build(&frame, MSG_TYPE_RAW_ADC,
                             (const uint8_t *)payload,
                             sizeof(frame_raw_adc_payload_t));
    if (rc < 0) {
        return (int)rc;
    }

    uart_send_frame(&frame);
    return 0;
}

