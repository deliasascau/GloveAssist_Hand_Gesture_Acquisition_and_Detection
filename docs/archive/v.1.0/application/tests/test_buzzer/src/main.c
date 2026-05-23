/*
 * Test Buzzer - PB4 GPIO + LED PC13 visual confirmation
 * Multiple frequencies test
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Buzzer on PB4 */
static const struct gpio_dt_spec buzzer = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 4,
	.dt_flags = GPIO_ACTIVE_HIGH
};

/* LED on PC13 (built-in, active LOW) */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

/* Software PWM beep function */
static void beep(uint32_t freq_hz, uint32_t duration_ms)
{
	uint32_t period_us = 1000000U / freq_hz;
	uint32_t half_period_us = period_us / 2U;
	uint32_t cycles = (duration_ms * 1000U) / period_us;
	
	for (uint32_t i = 0; i < cycles; i++) {
		gpio_pin_set_dt(&buzzer, 1);
		k_busy_wait(half_period_us);
		gpio_pin_set_dt(&buzzer, 0);
		k_busy_wait(half_period_us);
	}
}

int main(void)
{
	/* Init buzzer GPIO */
	if (!gpio_is_ready_dt(&buzzer)) {
		return -1;
	}
	gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE);
	
	/* Init LED GPIO */
	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	
	/* Main loop: LED blink + buzzer test cu frecvențe MAI JOASE */
	while (1) {
		/* LED ON - Beep LUNG la 500Hz (ton JOS) - 1.5 secunde */
		gpio_pin_set_dt(&led, 1);
		beep(500, 1500);
		k_sleep(K_MSEC(500));
		
		/* LED OFF - Beep LUNG la 800Hz (ton MEDIU) - 1.5 secunde */
		gpio_pin_set_dt(&led, 0);
		beep(800, 1500);
		k_sleep(K_MSEC(500));
		
		/* LED ON - Beep LUNG la 1200Hz (ton ÎNALT) - 1.5 secunde */
		gpio_pin_set_dt(&led, 1);
		beep(1200, 1500);
		k_sleep(K_MSEC(1000));
	}
	
	return 0;
}

