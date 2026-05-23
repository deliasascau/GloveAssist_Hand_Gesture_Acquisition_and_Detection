/*
 * Test Motor - PB6 PWM TIM4_CH1 + LED PC13
 * Folosește EXACT API-ul din aplicația principală!
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>

/* Motor PWM - EXACT ca în haptic_ui.c */
static const struct pwm_dt_spec motor_pwm = PWM_DT_SPEC_GET(DT_ALIAS(motor_pwm));

/* LED on PC13 (built-in, active LOW) */
static const struct gpio_dt_spec led = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 13,
	.dt_flags = GPIO_ACTIVE_LOW
};

int main(void)
{
	/* Check PWM device */
	if (!pwm_is_ready_dt(&motor_pwm)) {
		printk("ERROR: Motor PWM not ready!\n");
		return -1;
	}
	
	printk("Motor PWM ready: dev=%s, ch=%u, period=%u ns\n", 
	       motor_pwm.dev->name, motor_pwm.channel, motor_pwm.period);
	
	/* Init LED */
	if (!gpio_is_ready_dt(&led)) {
		return -1;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	
	printk("Motor vibration test starting...\n");
	
	/* Main loop: test different vibration intensities */
	while (1) {
		/* Pattern 1: LED ON - Weak vibration (20% duty cycle) */
		printk("Pattern 1: 20%% duty\n");
		gpio_pin_set_dt(&led, 1);
		pwm_set_dt(&motor_pwm, PWM_MSEC(1), PWM_MSEC(1) * 20 / 100);
		k_sleep(K_MSEC(1500));
		
		/* Stop */
		pwm_set_dt(&motor_pwm, PWM_MSEC(1), 0);
		k_sleep(K_MSEC(500));
		
		/* Pattern 2: LED OFF - Medium vibration (50% duty cycle) */
		printk("Pattern 2: 50%% duty\n");
		gpio_pin_set_dt(&led, 0);
		pwm_set_dt(&motor_pwm, PWM_MSEC(1), PWM_MSEC(1) * 50 / 100);
		k_sleep(K_MSEC(1500));
		
		/* Stop */
		pwm_set_dt(&motor_pwm, PWM_MSEC(1), 0);
		k_sleep(K_MSEC(500));
		
		/* Pattern 3: LED ON - Strong vibration (80% duty cycle) */
		printk("Pattern 3: 80%% duty\n");
		gpio_pin_set_dt(&led, 1);
		pwm_set_dt(&motor_pwm, PWM_MSEC(1), PWM_MSEC(1) * 80 / 100);
		k_sleep(K_MSEC(1500));
		
		/* Stop */
		pwm_set_dt(&motor_pwm, PWM_MSEC(1), 0);
		k_sleep(K_MSEC(1000));
	}
	
	return 0;
}
