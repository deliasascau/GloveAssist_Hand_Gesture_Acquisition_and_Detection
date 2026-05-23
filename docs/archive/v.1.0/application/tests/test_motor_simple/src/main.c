/*
 * Test Motor PWM Software - PB6 GPIO PWM manual (ca buzzerul)
 * PWM software pentru controlul intensității
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* Motor pe PB6 - PWM software */
static const struct gpio_dt_spec motor = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 6,
	.dt_flags = GPIO_ACTIVE_HIGH
};

/* LED pe PC13 (built-in, active LOW) */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

/* PWM software simplu - duty cycle ca procent 0-100 */
static void motor_pwm(uint8_t duty_percent, uint32_t duration_ms)
{
	if (duty_percent == 0) {
		/* Oprit */
		gpio_pin_set_dt(&motor, 0);
		k_sleep(K_MSEC(duration_ms));
		return;
	}
	
	if (duty_percent >= 100) {
		/* Full ON */
		gpio_pin_set_dt(&motor, 1);
		k_sleep(K_MSEC(duration_ms));
		return;
	}
	
	/* PWM software: 100Hz (10ms period) */
	uint32_t period_ms = 10;
	uint32_t on_time_ms = (period_ms * duty_percent) / 100;
	uint32_t off_time_ms = period_ms - on_time_ms;
	uint32_t cycles = duration_ms / period_ms;
	
	for (uint32_t i = 0; i < cycles; i++) {
		gpio_pin_set_dt(&motor, 1);
		k_msleep(on_time_ms);
		gpio_pin_set_dt(&motor, 0);
		k_msleep(off_time_ms);
	}
}

int main(void)
{
	/* Init motor GPIO */
	if (!gpio_is_ready_dt(&motor)) {
		return -1;
	}
	gpio_pin_configure_dt(&motor, GPIO_OUTPUT_INACTIVE);
	
	/* Init LED */
	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	
	printk("Motor PWM software - teste intensitate\n");
	
	/* Test diferite intensități PWM */
	while (1) {
		/* Pattern 1: Vibrație SLABĂ (20%) - LED ON */
		printk("Vibratie SLABA 20%%\n");
		gpio_pin_set_dt(&led, 1);
		motor_pwm(20, 1500);
		motor_pwm(0, 500);  /* Stop */
		
		/* Pattern 2: Vibrație MEDIE (50%) - LED OFF */
		printk("Vibratie MEDIE 50%%\n");
		gpio_pin_set_dt(&led, 0);
		motor_pwm(50, 1500);
		motor_pwm(0, 500);  /* Stop */
		
		/* Pattern 3: Vibrație PUTERNICĂ (80%) - LED ON */
		printk("Vibratie PUTERNICA 80%%\n");
		gpio_pin_set_dt(&led, 1);
		motor_pwm(80, 1500);
		motor_pwm(0, 1000);  /* Stop lung */
	}
	
	return 0;
}
