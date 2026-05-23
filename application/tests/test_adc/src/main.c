/*
 * test_adc — STM32 ADC Diagnostic
 *
 * Citeste 4 flex sensors (PA0-PA3) si trimite valorile ca ASCII simplu
 * pe USART1 (PA9 TX) la 115200 baud, la fiecare 500ms.
 *
 * Citire in PuTTY: conecteaza-te pe portul COM care are PA9 -> GPIO16 ESP32
 * Output asteptat:
 *   ADC0=1234  ADC1=0567  ADC2=2048  ADC3=3891  [ADC OK]
 *   sau:
 *   ADC0=0000  ADC1=0000  ADC2=0000  ADC3=0000  [INIT FAIL]
 *
 * LED PC13:
 *   Aprins fix (3s) = test pornit
 *   Clipeste lent (500ms) = ADC init esuat, toate valorile = 0
 *   Clipeste rapid (100ms) = ADC OK, valorile sunt reale
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(test_adc, LOG_LEVEL_INF);

#define NUM_CH 4U

static const struct adc_dt_spec adc_ch[NUM_CH] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 3),
};

static const struct gpio_dt_spec led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    /* LED init */
    if (device_is_ready(led.port)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
        /* Aprinde LED 3s = test pornit */
        gpio_pin_set_dt(&led, 1);
        k_msleep(3000);
        gpio_pin_set_dt(&led, 0);
    }

    LOG_INF("=== GloveAssist ADC Test ===");
    LOG_INF("Citesc PA0(ADC0) PA1(ADC1) PA2(ADC2) PA3(ADC3)");
    LOG_INF("Fiecare linie = 1 citire (500ms interval)");

    /* ADC init */
    bool adc_ok = true;
    for (uint8_t i = 0U; i < NUM_CH; i++) {
        if (!adc_is_ready_dt(&adc_ch[i])) {
            LOG_ERR("ADC ch%u NOT READY", i);
            adc_ok = false;
        } else {
            int r = adc_channel_setup_dt(&adc_ch[i]);
            if (r < 0) {
                LOG_ERR("ADC ch%u setup failed: %d", i, r);
                adc_ok = false;
            } else {
                LOG_INF("ADC ch%u OK", i);
            }
        }
    }

    if (!adc_ok) {
        LOG_ERR("ADC INIT ESUAT — verificati overlay-ul si pinii PA0-PA3");
    } else {
        LOG_INF("ADC init complet — incep citirile...");
    }

    /* Main loop */
    uint32_t sample = 0U;
    while (1) {
        int16_t val[NUM_CH] = {0, 0, 0, 0};

        if (adc_ok) {
            for (uint8_t i = 0U; i < NUM_CH; i++) {
                int16_t v = 0;
                struct adc_sequence seq = {
                    .buffer      = &v,
                    .buffer_size = sizeof(v),
                };
                adc_sequence_init_dt(&adc_ch[i], &seq);
                if (adc_read_dt(&adc_ch[i], &seq) == 0) {
                    val[i] = v;
                }
            }
        }

        /* Printeaza ca text simplu — citibil direct in PuTTY */
        LOG_INF("#%04u  ADC0=%4d  ADC1=%4d  ADC2=%4d  ADC3=%4d  %s",
                sample++,
                val[0], val[1], val[2], val[3],
                adc_ok ? "[OK]" : "[FAIL]");

        /* LED: lent=fail, rapid=ok */
        if (device_is_ready(led.port)) {
            int blink_ms = adc_ok ? 100 : 500;
            gpio_pin_set_dt(&led, 1);
            k_msleep(blink_ms);
            gpio_pin_set_dt(&led, 0);
            k_msleep(500 - blink_ms);
        } else {
            k_msleep(500);
        }
    }

    return 0;
}
