/*
 * Test LED PC13 - Built-in LED
 * Blink every 500ms
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* PC13 - built-in LED on Blue Pill */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW  /* Blue Pill LED is active LOW */
};

int main(void)
{
	/* Init LED GPIO */
	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	
	/* Blink loop: 500ms on, 500ms off */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(500);
	}
	
	return 0;
}
