/**
 * @file main.c
 * @brief GloveAssist STM32 firmware — entry point and thread definitions
 *
 * STM32F103 responsibilities:
 *   - ADC acquisition (4 flex sensors)
 *   - Gesture classification (rule-based + TinyML fallback)
 *   - UART communication to ESP32 (USART1: PA9 TX, PA10 RX @ 115200 baud)
 *   - OLED display (SSD1306 via I2C2)
 *   - Haptic feedback (motor TIM4_CH1 PB6, active buzzer GPIO PB4)
 *   - Safety: watchdog, sensor faults, ESP32 heartbeat monitor
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "common_types.h"
#include "safety_diag.h"
#include "security.h"
#include "sensor_logic.h"
#include "uart_comm.h"
#include "haptic_ui.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- Thread definitions (auto-started) ---------- */
K_THREAD_DEFINE(safety_tid, STACK_SAFETY,
                safety_thread_entry, NULL, NULL, NULL,
                PRIO_SAFETY_THREAD, 0, 0);

K_THREAD_DEFINE(sensor_tid, STACK_SENSOR,
                sensor_thread_entry, NULL, NULL, NULL,
                PRIO_SENSOR_THREAD, 0, 0);

K_THREAD_DEFINE(haptic_tid, STACK_HAPTIC,
                haptic_thread_entry, NULL, NULL, NULL,
                PRIO_HAPTIC_THREAD, 0, 0);

K_THREAD_DEFINE(uart_comm_tid, STACK_UART_COMM,
                uart_comm_thread_entry, NULL, NULL, NULL,
                PRIO_UART_COMM_THREAD, 0, 0);

/* ---------- Shared message queue: sensor -> UART TX ---------- */
K_MSGQ_DEFINE(sensor_msgq, sizeof(sensor_packet_t), 8, 4);

int main(void)
{
    LOG_INF("=== GloveAssist STM32 firmware ===");
    LOG_INF("Threads: safety(P%d), uart_comm(P%d), sensor(P%d)",
            PRIO_SAFETY_THREAD, PRIO_UART_COMM_THREAD,
            PRIO_SENSOR_THREAD);

    /* Initialise safety subsystem (watchdog + heartbeat monitor) */
    int ret = safety_init();
    if (ret < 0) {
        LOG_ERR("Safety init failed: %d — continuing in degraded mode", ret);
    }

    ret = security_init();
    if (ret < 0) {
        LOG_ERR("Security init failed: %d", ret);
    }

    LOG_INF("All threads auto-started — main() returning to idle");
    return 0;
}
