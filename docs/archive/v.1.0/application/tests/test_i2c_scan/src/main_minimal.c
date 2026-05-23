/*
 * Test I2C2 MINIMAL - doar device_is_ready()
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>

static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

int main(void)
{
	const struct device *i2c_dev;
	
	/* Init LED */
	if (!gpio_is_ready_dt(&led)) {
		while(1);
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT);
	
	/* Indicator 1: LED ON = boot OK */
	gpio_pin_set_dt(&led, 1);
	k_msleep(500);
	
	/* Indicator 2: LED OFF = înainte de I2C get */
	gpio_pin_set_dt(&led, 0);
	k_msleep(500);
	
	/* Get I2C2 device */
	gpio_pin_set_dt(&led, 1);  /* ON înainte de get */
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c2));
	gpio_pin_set_dt(&led, 0);  /* OFF după get */
	k_msleep(500);
	
	/* Check ready */
	gpio_pin_set_dt(&led, 1);  /* ON înainte de ready check */
	if (!device_is_ready(i2c_dev)) {
		/* I2C2 NU e ready - LED încet 2s */
		while (1) {
			gpio_pin_toggle_dt(&led);
			k_msleep(2000);
		}
	}
	gpio_pin_set_dt(&led, 0);  /* OFF după ready check */
	
	/* SUCCESS - I2C2 e ready - LED rapid 200ms */
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(200);
	}
	
	return 0;
}
