/*
 * Test ABSOLUT MINIMAL - doar LED ON
 * Confirmă că firmware-ul bootează și ajunge în main()
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>

/* LED PC13 (built-in, active LOW) */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

int main(void)
{
	/* LED ON imediat - confirmă boot */
	if (gpio_is_ready_dt(&led)) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	}
	
	/* Clipire foarte rapidă - 50ms ON/OFF */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(50);
	}
	
	return 0;
}
