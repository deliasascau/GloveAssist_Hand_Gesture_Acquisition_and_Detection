#include "alert.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(alert, LOG_LEVEL_INF);

/* ADC threshold for HELP gesture detection (mid-scale 12-bit). */
static const uint16_t ALERT_FLEX_THRESH = 2048U;

/* DT aliases defined in overlay:
 *   alert-out -> PB0 (ALERT output to ESP32)
 *   alert-in  -> PB1 (ACK  input from ESP32)
 */
#define ALERT_OUT_NODE DT_ALIAS(alert_out)
#define ALERT_IN_NODE  DT_ALIAS(alert_in)

static const struct gpio_dt_spec s_alert_out =
    GPIO_DT_SPEC_GET(ALERT_OUT_NODE, gpios);
static const struct gpio_dt_spec s_alert_in =
    GPIO_DT_SPEC_GET(ALERT_IN_NODE, gpios);

void alert_init(void)
{
    if (!gpio_is_ready_dt(&s_alert_out)) {
        LOG_ERR("ALERT_OUT (PB0) not ready");
        return;
    }
    if (!gpio_is_ready_dt(&s_alert_in)) {
        LOG_ERR("ALERT_IN (PB1) not ready");
        return;
    }

    (void)gpio_pin_configure_dt(&s_alert_out, GPIO_OUTPUT_INACTIVE);
    (void)gpio_pin_configure_dt(&s_alert_in,  GPIO_INPUT | GPIO_PULL_DOWN);

    LOG_INF("Alert GPIO init OK (PB0=OUT PB1=IN)");
}

void alert_raise(void)
{
    (void)gpio_pin_set_dt(&s_alert_out, 1);
}

void alert_clear(void)
{
    (void)gpio_pin_set_dt(&s_alert_out, 0);
}

bool alert_ack_received(void)
{
    return (gpio_pin_get_dt(&s_alert_in) > 0);
}

bool alert_is_help_gesture(const uint16_t adc[4])
{
    /* HELP = full fist: all 4 fingers bent above ALERT_FLEX_THRESH.
     * Mirrors GESTURE_HELP (ALL_FINGERS bent) in gesture.c on ESP32 side. */
    return ((adc[0] > ALERT_FLEX_THRESH)
            && (adc[1] > ALERT_FLEX_THRESH)
            && (adc[2] > ALERT_FLEX_THRESH)
            && (adc[3] > ALERT_FLEX_THRESH));
}
