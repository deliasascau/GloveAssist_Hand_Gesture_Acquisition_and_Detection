/*
 * Test PB4 Slow Toggle - pentru verificare cu LED/multimetru
 * PB4 clipește 1Hz (ca PC13) pentru a verifica că pinul comută
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* PB4 pentru buzzer */
static const struct gpio_dt_spec pb4 = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 4,
	.dt_flags = GPIO_ACTIVE_HIGH
};

/* LED PC13 pentru referință */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

int main(void)
{
	/* Init PB4 */
	if (!gpio_is_ready_dt(&pb4)) {
		return -1;
	}
	gpio_pin_configure_dt(&pb4, GPIO_OUTPUT_INACTIVE);
	
	/* Init LED */
	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	
	/* Test SIMPLU: PB4 HIGH 2s → LOW 2s (fără PWM) */
	/* LED sincronizat: ON când PB4=HIGH, OFF când PB4=LOW */
	
	while (1) {
		/* PB4 HIGH + LED ON - 2 secunde */
		gpio_pin_set_dt(&pb4, 1);    /* 3.3V pe PB4 */
		gpio_pin_set_dt(&led, 1);    /* LED aprins */
		k_sleep(K_MSEC(2000));
		
		/* PB4 LOW + LED OFF - 2 secunde */
		gpio_pin_set_dt(&pb4, 0);    /* 0V pe PB4 */
		gpio_pin_set_dt(&led, 0);    /* LED stins */
		k_sleep(K_MSEC(2000));
	}
	
	return 0;
}
