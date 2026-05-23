/*
 * Test I2C Diagnostic - Verificare profesională hardware
 * LED pattern arată starea fiecărei adrese testate
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

/* LED PC13 (active LOW) */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

/* Blink pattern pentru indicare */
void blink_pattern(int count, int delay_ms)
{
	for (int i = 0; i < count; i++) {
		gpio_pin_set_dt(&led, 1);
		k_msleep(delay_ms / 2);
		gpio_pin_set_dt(&led, 0);
		k_msleep(delay_ms / 2);
	}
}

/* Test adrese specifice OLED cu READ transaction */
int test_addresses(const struct device *i2c)
{
	uint8_t test_addrs[] = {0x3C, 0x3D};  /* 7-bit addresses */
	int found = 0;
	
	printk("\n=== Test adrese OLED (7-bit, WRITE+READ) ===\n");
	
	for (int i = 0; i < 2; i++) {
		uint8_t addr = test_addrs[i];
		uint8_t dummy;
		int ret_write, ret_read;
		
		/* Test 1: WRITE transaction */
		struct i2c_msg msg_write = {
			.buf = &dummy,
			.len = 0,
			.flags = I2C_MSG_WRITE | I2C_MSG_STOP
		};
		ret_write = i2c_transfer(i2c, &msg_write, 1, addr);
		
		k_msleep(10);  /* Delay între transactions */
		
		/* Test 2: READ transaction (unele device-uri răspund doar la read) */
		struct i2c_msg msg_read = {
			.buf = &dummy,
			.len = 1,
			.flags = I2C_MSG_READ | I2C_MSG_STOP
		};
		ret_read = i2c_transfer(i2c, &msg_read, 1, addr);
		
		printk("Adresa 0x%02X: WRITE=%s, READ=%s\n", 
		       addr, 
		       (ret_write == 0) ? "OK" : "FAIL",
		       (ret_read == 0) ? "OK" : "FAIL");
		
		if (ret_write == 0 || ret_read == 0) {
			found++;
			printk("*** GASIT la 0x%02X! ***\n", addr);
			/* Blink rapid pentru fiecare device găsit */
			blink_pattern(5, 100);
			k_msleep(500);
		}
		
		k_msleep(50);  /* Delay între adrese */
	}
	
	return found;
}

/* Scan complet I2C */
int full_scan(const struct device *i2c)
{
	int found = 0;
	
	printk("\n=== Scan complet I2C (0x03-0x77) ===\n");
	printk("Adrese gasite: ");
	
	for (uint8_t addr = 0x03; addr < 0x78; addr++) {
		uint8_t dummy;
		
		struct i2c_msg msg = {
			.buf = &dummy,
			.len = 0,
			.flags = I2C_MSG_WRITE | I2C_MSG_STOP
		};
		
		if (i2c_transfer(i2c, &msg, 1, addr) == 0) {
			printk("0x%02X ", addr);
			found++;
			
			/* Quick blink pentru fiecare device */
			gpio_pin_toggle_dt(&led);
			k_msleep(50);
			gpio_pin_toggle_dt(&led);
			k_msleep(150);
		}
	}
	
	if (found == 0) {
		printk("NIMIC!\n");
	} else {
		printk("\nTotal: %d device(s)\n", found);
	}
	
	return found;
}

int main(void)
{
	const struct device *i2c_dev;
	int found_specific, found_total;
	
	printk("\n========================================\n");
	printk("   I2C DIAGNOSTIC - SLOW MODE\n");
	printk("   Clock: 10kHz + Pull-ups\n");
	printk("========================================\n");
	
	/* Init LED */
	if (!gpio_is_ready_dt(&led)) {
		printk("ERROR: LED not ready\n");
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	printk("[OK] LED init\n");
	
	/* 3 blinks boot */
	blink_pattern(3, 200);
	
	/* DELAY LUNG pentru OLED power-on! */
	printk("Waiting 2s for OLED power-on...\n");
	k_msleep(2000);
	
	/* Get I2C2 */
	printk("Getting I2C2 device...\n");
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c2));
	
	if (!device_is_ready(i2c_dev)) {
		printk("ERROR: I2C2 not ready!\n");
		printk("Verifică: PB10/PB11 conectați? Hardware OK?\n");
		
		/* Blink FOARTE LENT = I2C nu funcționează */
		while (1) {
			gpio_pin_toggle_dt(&led);
			k_msleep(3000);
		}
	}
	
	printk("[OK] I2C2 ready\n");
	blink_pattern(2, 300);
	k_msleep(1000);
	
	/* Test 1: Adrese OLED specifice */
	found_specific = test_addresses(i2c_dev);
	k_msleep(1000);
	
	/* Test 2: Scan complet */
	found_total = full_scan(i2c_dev);
	
	printk("\n========================================\n");
	printk("REZULTATE:\n");
	printk("  Adrese OLED (0x3C/3D/78/7A): %d\n", found_specific);
	printk("  Total device-uri I2C: %d\n", found_total);
	printk("========================================\n");
	
	if (found_total == 0) {
		printk("\n!!! NIMIC GASIT !!!\n");
		printk("Verifică:\n");
		printk("  1. OLED primeşte 3.3V pe VCC?\n");
		printk("  2. GND conectat?\n");
		printk("  3. Fire Dupont OK (încearcă alte fire)?\n");
		printk("  4. PB10 → SCL (OLED)\n");
		printk("  5. PB11 → SDA (OLED)\n");
		printk("  6. OLED funcțional? (testează pe alt MCU)\n");
		
		/* Pattern: 1 blink lung, 2 blinks scurte = NIMIC GASIT */
		while (1) {
			gpio_pin_set_dt(&led, 1);
			k_msleep(1000);
			gpio_pin_set_dt(&led, 0);
			k_msleep(500);
			
			blink_pattern(2, 200);
			k_msleep(2000);
		}
	} else {
		printk("\n*** SUCCESS! ***\n");
		printk("OLED găsit și funcțional!\n");
		
		/* Blink rapid continuu = SUCCESS */
		while (1) {
			gpio_pin_toggle_dt(&led);
			k_msleep(150);
		}
	}
	
	return 0;
}
