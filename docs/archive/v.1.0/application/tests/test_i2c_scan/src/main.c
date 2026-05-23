/*
 * I2C Scanner - detectează dispozitive pe I2C2 (PB10/PB11)
 * LED rapid = scan OK, LED încet = I2C eroare
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>

/* LED PC13 (built-in, active LOW) */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

int main(void)
{
	const struct device *i2c_dev;
	int ret;
	uint8_t found_count = 0;
	
	printk("\n=== I2C2 Scanner Start ===\n");
	
	/* Init LED */
	if (!gpio_is_ready_dt(&led)) {
		printk("ERROR: LED not ready\n");
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	
	/* Boot indicator - rapid blink 3 ori */
	for (int i = 0; i < 3; i++) {
		gpio_pin_toggle_dt(&led);
		k_msleep(100);
	}
	gpio_pin_set_dt(&led, 1);  /* ON */
	
	/* Get I2C2 device */
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c2));
	
	if (!device_is_ready(i2c_dev)) {
		printk("ERROR: I2C2 device not ready!\n");
		
		/* LED foarte încet = I2C2 nu e ready */
		while (1) {
			gpio_pin_toggle_dt(&led);
			k_msleep(2000);
		}
	}
	
	printk("I2C2 device ready, scanning addresses 0x03-0x77...\n");
	
	/* Scan I2C bus - adrese 0x03 to 0x77 (7-bit addressing) */
	for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
		struct i2c_msg msg;
		uint8_t dummy_data = 0;
		
		/* Try to read 1 byte */
		msg.buf = &dummy_data;
		msg.len = 1;
		msg.flags = I2C_MSG_READ | I2C_MSG_STOP;
		
		ret = i2c_transfer(i2c_dev, &msg, 1, addr);
		
		if (ret == 0) {
			printk("  [FOUND] Device at 0x%02X\n", addr);
			found_count++;
			
			/* Blink LED pentru fiecare dispozitiv găsit */
			for (int i = 0; i < 3; i++) {
				gpio_pin_toggle_dt(&led);
				k_msleep(50);
			}
			gpio_pin_set_dt(&led, 1);
		}
		
		k_msleep(10);  /* Delay între probe */
	}
	
	printk("\nScan complete. Found %d device(s).\n", found_count);
	
	if (found_count == 0) {
		printk("VERIFICĂ: PB10(SCL), PB11(SDA), 3.3V, GND conectate?\n");
		
		/* LED încet = nimic găsit */
		while (1) {
			gpio_pin_toggle_dt(&led);
			k_msleep(1000);
		}
	} else {
		printk("SUCCESS! LED clipește rapid.\n");
		
		/* LED rapid = dispozitive găsite */
		while (1) {
			gpio_pin_toggle_dt(&led);
			k_msleep(200);
		}
	}
	
	return 0;
}
