/*
 * OLED direct I2C test on PB10/PB11 (I2C2).
 * Tries both addresses (0x3C, 0x3D) and both controller init styles (SSD1306/SH1106).
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <string.h>

static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

static const uint8_t oled_addrs[] = {0x3C, 0x3D};

static void blink_ms(int delay_ms)
{
	gpio_pin_toggle_dt(&led);
	k_msleep(delay_ms);
}

static int oled_cmd(const struct device *i2c, uint8_t addr, uint8_t cmd)
{
	uint8_t tx[2] = {0x00, cmd};
	return i2c_write(i2c, tx, sizeof(tx), addr);
}

static int oled_send_cmds(const struct device *i2c, uint8_t addr,
				  const uint8_t *cmds, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		int ret = oled_cmd(i2c, addr, cmds[i]);
		if (ret) {
			return ret;
		}
	}
	return 0;
}

static int oled_init_ssd1306(const struct device *i2c, uint8_t addr)
{
	static const uint8_t seq[] = {
		0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
		0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
		0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
	};

	return oled_send_cmds(i2c, addr, seq, sizeof(seq));
}

static int oled_init_sh1106(const struct device *i2c, uint8_t addr)
{
	static const uint8_t seq[] = {
		0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
		0xAD, 0x8B, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x80,
		0xD9, 0x22, 0xDB, 0x35, 0xA4, 0xA6, 0xAF,
	};

	return oled_send_cmds(i2c, addr, seq, sizeof(seq));
}

static int oled_fill(const struct device *i2c, uint8_t addr,
			   uint8_t pattern, uint8_t col_offset)
{
	uint8_t data[129];
	data[0] = 0x40;
	memset(&data[1], pattern, 128);

	for (uint8_t page = 0; page < 8; page++) {
		int ret;

		ret = oled_cmd(i2c, addr, 0xB0 + page);
		if (ret) {
			return ret;
		}

		ret = oled_cmd(i2c, addr, 0x00 + (col_offset & 0x0F));
		if (ret) {
			return ret;
		}

		ret = oled_cmd(i2c, addr, 0x10 + ((col_offset >> 4) & 0x0F));
		if (ret) {
			return ret;
		}

		ret = i2c_write(i2c, data, sizeof(data), addr);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int oled_fill_checker(const struct device *i2c, uint8_t addr, uint8_t col_offset)
{
	uint8_t data[129];
	data[0] = 0x40;

	for (uint8_t page = 0; page < 8; page++) {
		int ret;
		uint8_t p = (page & 1U) ? 0xAA : 0x55;

		memset(&data[1], p, 128);

		ret = oled_cmd(i2c, addr, 0xB0 + page);
		if (ret) {
			return ret;
		}

		ret = oled_cmd(i2c, addr, 0x00 + (col_offset & 0x0F));
		if (ret) {
			return ret;
		}

		ret = oled_cmd(i2c, addr, 0x10 + ((col_offset >> 4) & 0x0F));
		if (ret) {
			return ret;
		}

		ret = i2c_write(i2c, data, sizeof(data), addr);
		if (ret) {
			return ret;
		}
	}

	return 0;
}

static int run_oled_cycle(const struct device *i2c, uint8_t addr, bool sh1106)
{
	uint8_t col_offset = sh1106 ? 2U : 0U;
	int ret;

	ret = oled_fill(i2c, addr, 0xFF, col_offset);
	if (ret) {
		return ret;
	}
	k_msleep(500);

	ret = oled_fill(i2c, addr, 0x00, col_offset);
	if (ret) {
		return ret;
	}
	k_msleep(500);

	ret = oled_fill_checker(i2c, addr, col_offset);
	if (ret) {
		return ret;
	}
	k_msleep(500);

	return 0;
}

int main(void)
{
	const struct device *i2c;
	uint8_t active_addr = 0xFF;
	bool active_sh1106 = false;
	int ret = 0;

	printk("\n=== OLED Direct I2C Test (PB10/PB11) ===\n");

	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

	for (int i = 0; i < 4; i++) {
		blink_ms(100);
	}

	i2c = DEVICE_DT_GET(DT_NODELABEL(i2c2));
	if (!device_is_ready(i2c)) {
		printk("ERROR: I2C2 not ready\n");
		while (1) {
			blink_ms(1200);
		}
	}

	printk("I2C2 ready. Probing 0x3C/0x3D...\n");

	for (size_t i = 0; i < ARRAY_SIZE(oled_addrs); i++) {
		uint8_t addr = oled_addrs[i];

		ret = oled_cmd(i2c, addr, 0xAE);
		if (ret) {
			printk("Addr 0x%02X no ACK (%d)\n", addr, ret);
			continue;
		}

		printk("Addr 0x%02X ACK. Trying SSD1306 init...\n", addr);
		ret = oled_init_ssd1306(i2c, addr);
		if (!ret) {
			active_addr = addr;
			active_sh1106 = false;
			break;
		}

		printk("SSD1306 init failed (%d). Trying SH1106 init...\n", ret);
		ret = oled_init_sh1106(i2c, addr);
		if (!ret) {
			active_addr = addr;
			active_sh1106 = true;
			break;
		}

		printk("SH1106 init failed (%d) on 0x%02X\n", ret, addr);
	}

	if (active_addr == 0xFF) {
		printk("ERROR: No OLED ACK/init on 0x3C/0x3D via PB10/PB11\n");
		while (1) {
			blink_ms(1000);
		}
	}

	printk("OLED active at 0x%02X (%s)\n",
	       active_addr, active_sh1106 ? "SH1106-mode" : "SSD1306-mode");

	while (1) {
		ret = run_oled_cycle(i2c, active_addr, active_sh1106);
		if (ret) {
			printk("Write cycle failed (%d)\n", ret);
			blink_ms(900);
		} else {
			blink_ms(120);
		}
	}
}
