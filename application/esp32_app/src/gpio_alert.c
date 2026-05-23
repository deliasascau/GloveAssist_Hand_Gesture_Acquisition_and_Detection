#include "gpio_alert.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_alert, LOG_LEVEL_INF);

/* DT aliases defined in overlay:
 *   alert-in  -> GPIO18 (receives ALERT from STM32 PB0)
 *   ack-out   -> GPIO19 (sends ACK to STM32 PB1)
 */
#define ALERT_IN_NODE DT_ALIAS(alert_in)
#define ACK_OUT_NODE  DT_ALIAS(ack_out)

static const struct gpio_dt_spec s_alert_in =
    GPIO_DT_SPEC_GET(ALERT_IN_NODE, gpios);
static const struct gpio_dt_spec s_ack_out =
    GPIO_DT_SPEC_GET(ACK_OUT_NODE, gpios);

static volatile bool s_alert_flag;
static struct gpio_callback s_alert_cb;

static void alert_isr(const struct device *dev, struct gpio_callback *cb,
                      uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    s_alert_flag = true;
}

int gpio_alert_init(void)
{
    if (!gpio_is_ready_dt(&s_alert_in)) {
        LOG_ERR("ALERT_IN (GPIO18) not ready");
        return -ENODEV;
    }
    if (!gpio_is_ready_dt(&s_ack_out)) {
        LOG_ERR("ACK_OUT (GPIO19) not ready");
        return -ENODEV;
    }

    (void)gpio_pin_configure_dt(&s_alert_in, GPIO_INPUT | GPIO_PULL_DOWN);
    (void)gpio_pin_configure_dt(&s_ack_out,  GPIO_OUTPUT_INACTIVE);

    gpio_init_callback(&s_alert_cb, alert_isr, BIT(s_alert_in.pin));
    (void)gpio_add_callback(s_alert_in.port, &s_alert_cb);
    (void)gpio_pin_interrupt_configure_dt(&s_alert_in,
                                          GPIO_INT_EDGE_RISING);

    LOG_INF("GPIO alert init OK (GPIO18=IN GPIO19=OUT)");
    return 0;
}

bool gpio_alert_poll(void)
{
    if (!s_alert_flag) {
        return false;
    }
    s_alert_flag = false;

    LOG_WRN("ALERT pin raised by STM32 — emergency gesture detected!");

    /* Pulse ACK high for 200 ms so STM32 knows we got it. */
    (void)gpio_pin_set_dt(&s_ack_out, 1);
    k_msleep(200);
    (void)gpio_pin_set_dt(&s_ack_out, 0);

    return true;
}
