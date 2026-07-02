/*
 * haptic.h — STM32 actuator and display interface.
 *
 * Controls:
 *   OLED SSD1306 128×64 via Zephyr Display API (I2C2 PB10/PB11)
 *   Motor vibration via hardware PWM TIM4_CH1 (PB6)
 *   Passive buzzer via hardware PWM TIM3_CH1 (PB4)
 *   Built-in LED (PC13, active-low)
 */

#ifndef HAPTIC_H_
#define HAPTIC_H_

#include <stdint.h>
#include <stdbool.h>

/** @brief Initialise all actuators (must be called once from main). */
void haptic_init(void);

/*
 * Async interface used by the STM32 main/UART path.
 * These functions enqueue work for the haptic thread and return immediately.
 */
void haptic_post_oled_show(const char *line1, const char *line2);
void haptic_post_buzzer_beep(uint8_t count, uint32_t pulse_ms, uint32_t gap_ms);
void haptic_post_motor_pulse(uint32_t duty_pct, uint32_t dur_ms);
void haptic_post_caregiver_ack(uint8_t gesture_id);
void haptic_post_gesture_feedback(uint8_t gesture_id);
void haptic_post_led_ack(uint8_t count);
void haptic_post_bad_cmd_diag(void);
void haptic_post_oled_calib_show(uint8_t state, uint8_t idx, uint8_t mid,
                                 uint8_t rng, uint8_t pnk);

/** @brief Show up to two text lines on the SSD1306 OLED (pages 0 and 2). */
void haptic_oled_show(const char *line1, const char *line2);

/**
 * @brief Show four text lines on the OLED (pages 0, 2, 4, 6 — fiecare page = 8 pixel rows).
 *        NULL pentru o linie o lasa goala.
 */
void haptic_oled_show4(const char *l0, const char *l2,
                       const char *l4, const char *l6);

/**
 * @brief Tick de idle OLED — apelat din main loop.
 *        Dupa 5s fara niciun update OLED, afiseaza "GloveAssist / Activ".
 *        Foloseste flush raw (nu reseteaza timer-ul de activitate).
 *
 * Citeste now intern cu k_uptime_get_32() pentru a evita underflow uint32
 * cauzat de tick_cmd() care poate bloca 200-1000ms.
 */
void haptic_oled_idle_tick(void);

/** @brief Generate a hardware PWM tone at @p freq_hz Hz for @p duration_ms ms. */
void haptic_tone(uint32_t freq_hz, uint32_t duration_ms);

/** @brief Beep the passive buzzer @p count times. */
void haptic_buzzer_beep(uint8_t count, uint32_t pulse_ms, uint32_t gap_ms);

/** @brief Play boot-up welcome melody (C-E-G arpeggio). */
void haptic_boot_melody(void);

/** @brief Set motor PWM duty cycle continuously (0 = off, 100 = full). */
void haptic_motor_duty(uint32_t duty_pct);

/** @brief Run motor at @p duty_pct for @p dur_ms ms, then stop. */
void haptic_motor_pulse(uint32_t duty_pct, uint32_t dur_ms);

/**
 * @brief Play the full caregiver-ACK feedback sequence.
 *
 * Shows a gesture-specific confirmation on the OLED (e.g. "WATER: vine!"),
 * plays an ascending C6-E6-G6-C7 melody, and runs a dit-dit-DIT motor pattern.
 *
 * @param gesture_id  The gesture that was acknowledged (gesture_id_t cast to uint8_t).
 *                    Use GESTURE_NONE (0) when the gesture is unknown.
 */
void haptic_caregiver_ack(uint8_t gesture_id);

/**
 * @brief Play the per-gesture unique haptic+OLED feedback.
 *
 * Each gesture has a distinct tone melody + vibration pattern:
 *   WATER  (1): single C5 tone + 1 motor pulse
 *   WC     (2): two-tone A4→A5 + 2 motor pulses
 *   FOOD   (3): rising 3-note C5-E5-G5 + 3 motor pulses
 *   HELP   (4): 4 rapid alarm beeps + strong 500ms motor
 *
 * @param gesture_id  gesture_id_t cast to uint8_t.
 */
void haptic_gesture_feedback(uint8_t gesture_id);

/** @brief Set built-in LED (on = lit, the GPIO polarity is handled internally). */
void haptic_led_set(bool on);

/** @brief Toggle built-in LED. */
void haptic_led_toggle(void);

/**
 * @brief Show live calibration display on all 4 OLED rows.
 *
 * @param state  0..3 = OPEN phase (Gata:3/2/1/TINE!), 4..7 = FIST phase
 * @param idx    Index  finger ADC>>4 (0-255, 0=bent, ~200=extended)
 * @param mid    Middle finger ADC>>4
 * @param rng    Ring   finger ADC>>4
 * @param pnk    Pinky  finger ADC>>4
 */
void haptic_oled_calib_show(uint8_t state, uint8_t idx, uint8_t mid,
                             uint8_t rng, uint8_t pnk);

#endif /* HAPTIC_H_ */
