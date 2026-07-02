/*
 * haptic.c — STM32 actuator and display driver.
 *
 * OLED:   Zephyr Display API on SSD1306 (I2C2, DT nodelabel ssd1306).
 * Motor:  Zephyr PWM API on TIM4_CH1 PB6 (DT alias pwm-mot → motor_pwm).
 * Buzzer: Zephyr PWM API on TIM3_CH1 PB4 (DT alias pwm-buzz).
 * LED:    GPIO active-low (DT alias led0 = PC13).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include "haptic.h"

/* ── Device bindings ─────────────────────────────────────────────────── */
static const struct device      *s_oled   = DEVICE_DT_GET(DT_NODELABEL(ssd1306));
static const struct pwm_dt_spec  s_motor  = PWM_DT_SPEC_GET(DT_ALIAS(pwm_mot));
static const struct pwm_dt_spec  s_buzzer = PWM_DT_SPEC_GET(DT_ALIAS(pwm_buzz));
static const struct gpio_dt_spec s_led    = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static bool     s_oled_ready;
static uint32_t s_oled_activity_time;

#define HAPTIC_QUEUE_LEN          10U
#define HAPTIC_THREAD_STACK_SIZE  1536U
#define HAPTIC_THREAD_PRIORITY    6
#define HAPTIC_TEXT_LEN           17U

typedef enum {
    HAPTIC_MSG_OLED_SHOW,
    HAPTIC_MSG_BUZZER_BEEP,
    HAPTIC_MSG_MOTOR_PULSE,
    HAPTIC_MSG_CAREGIVER_ACK,
    HAPTIC_MSG_GESTURE_FEEDBACK,
    HAPTIC_MSG_LED_ACK,
    HAPTIC_MSG_BAD_CMD_DIAG,
    HAPTIC_MSG_OLED_CALIB,
} haptic_msg_type_t;

typedef struct {
    haptic_msg_type_t type;
    uint8_t           arg0;
    uint8_t           arg1;
    uint8_t           arg2;
    uint8_t           arg3;
    uint8_t           arg4;
    uint32_t          duration_ms;
    uint32_t          gap_ms;
    char              line1[HAPTIC_TEXT_LEN];
    char              line2[HAPTIC_TEXT_LEN];
} haptic_msg_t;

static void haptic_thread(void *a, void *b, void *c);

K_MSGQ_DEFINE(s_haptic_msgq, sizeof(haptic_msg_t), HAPTIC_QUEUE_LEN, 4);
K_MUTEX_DEFINE(s_haptic_io_lock);
K_THREAD_DEFINE(s_haptic_tid, HAPTIC_THREAD_STACK_SIZE, haptic_thread,
                NULL, NULL, NULL, HAPTIC_THREAD_PRIORITY, 0, 0);

/* ── 5×7 pixel font — ASCII 0x20 (' ') .. 0x7A ('z') ───────────────── */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' '  0x20 */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!'       */
    {0x00,0x07,0x00,0x07,0x00}, /* '"'       */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#'       */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$'       */
    {0x23,0x13,0x08,0x64,0x62}, /* '%'       */
    {0x36,0x49,0x55,0x22,0x50}, /* '&'       */
    {0x00,0x05,0x03,0x00,0x00}, /* '\''      */
    {0x00,0x1C,0x22,0x41,0x00}, /* '('       */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')'       */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*'       */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+'       */
    {0x00,0x50,0x30,0x00,0x00}, /* ','       */
    {0x08,0x08,0x08,0x08,0x08}, /* '-'       */
    {0x00,0x60,0x60,0x00,0x00}, /* '.'       */
    {0x20,0x10,0x08,0x04,0x02}, /* '/'       */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0'       */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1'       */
    {0x42,0x61,0x51,0x49,0x46}, /* '2'       */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3'       */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4'       */
    {0x27,0x45,0x45,0x45,0x39}, /* '5'       */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6'       */
    {0x01,0x71,0x09,0x05,0x03}, /* '7'       */
    {0x36,0x49,0x49,0x49,0x36}, /* '8'       */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9'       */
    {0x00,0x36,0x36,0x00,0x00}, /* ':'       */
    {0x00,0x56,0x36,0x00,0x00}, /* ';'       */
    {0x08,0x14,0x22,0x41,0x00}, /* '<'       */
    {0x14,0x14,0x14,0x14,0x14}, /* '='       */
    {0x00,0x41,0x22,0x14,0x08}, /* '>'       */
    {0x02,0x01,0x51,0x09,0x06}, /* '?'       */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@'       */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A'       */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B'       */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C'       */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D'       */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E'       */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F'       */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G'       */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H'       */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I'       */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J'       */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K'       */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L'       */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M'       */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N'       */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O'       */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P'       */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q'       */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R'       */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S'       */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T'       */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U'       */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V'       */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W'       */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X'       */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y'       */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z'       */
    {0x00,0x7F,0x41,0x41,0x00}, /* '['       */
    {0x02,0x04,0x08,0x10,0x20}, /* '\\'      */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']'       */
    {0x04,0x02,0x01,0x02,0x04}, /* '^'       */
    {0x40,0x40,0x40,0x40,0x40}, /* '_'       */
    {0x00,0x01,0x02,0x04,0x00}, /* '`'       */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a'       */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b'       */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c'       */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd'       */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e'       */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f'       */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g'       */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h'       */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i'       */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j'       */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k'       */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l'       */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm'       */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n'       */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o'       */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p'       */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q'       */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r'       */
    {0x48,0x54,0x54,0x54,0x20}, /* 's'       */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't'       */
    {0x3C,0x40,0x40,0x40,0x3C}, /* 'u'       */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v'       */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w'       */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x'       */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y'       */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z'  0x7A */
};

/* ── OLED framebuffer: 128 cols × 8 pages (8 rows/page = 64 rows) ─── */
static uint8_t s_fb[128U * 8U];

static void oled_draw_char(uint8_t x, uint8_t page, char c)
{
    if ((c < 0x20) || (c > 0x7A)) {
        c = ' ';
    }
    const uint8_t *g = font5x7[(uint8_t)c - 0x20U];
    for (uint8_t col = 0U; col < 5U; col++) {
        if ((x + col) < 128U) {
            s_fb[(uint16_t)(page * 128U) + x + col] = g[col];
        }
    }
    if ((x + 5U) < 128U) {
        s_fb[(uint16_t)(page * 128U) + x + 5U] = 0x00U;
    }
}

static void oled_draw_str(uint8_t x, uint8_t page, const char *s)
{
    while ((*s != '\0') && (x < 122U)) {
        oled_draw_char(x, page, *s);
        x = (uint8_t)(x + 6U);
        s++;
    }
}

static void oled_flush_raw(void)
{
    if (!s_oled_ready) {
        return;
    }
    struct display_buffer_descriptor desc = {
        .buf_size = sizeof(s_fb),
        .width    = 128U,
        .height   = 64U,
        .pitch    = 128U,
    };
    (void)display_write(s_oled, 0, 0, &desc, s_fb);
}

static void oled_flush(void)
{
    s_oled_activity_time = k_uptime_get_32();
    oled_flush_raw();
}

/* Draw up to 4 text lines on pages 0, 2, 4, 6 (each page = 8 pixel rows). */
static void oled_show4(const char *l0, const char *l2,
                       const char *l4, const char *l6)
{
    (void)memset(s_fb, 0, sizeof(s_fb));
    if (l0 != NULL) { oled_draw_str(0U, 0U, l0); }
    if (l2 != NULL) { oled_draw_str(0U, 2U, l2); }
    if (l4 != NULL) { oled_draw_str(0U, 4U, l4); }
    if (l6 != NULL) { oled_draw_str(0U, 6U, l6); }
    oled_flush();
}

/* ── Motor (PWM) ─────────────────────────────────────────────────────── */
static void motor_set_duty(uint32_t duty_pct)
{
    if (!pwm_is_ready_dt(&s_motor)) {
        return;
    }
    const uint32_t period_ns = 1000000U; /* 1 kHz */
    uint32_t pulse_ns = (period_ns * duty_pct) / 100U;
    (void)pwm_set_dt(&s_motor, period_ns, pulse_ns);
}

/* ── Public API ──────────────────────────────────────────────────────── */

static void haptic_copy_text(char dst[HAPTIC_TEXT_LEN], const char *src)
{
    (void)memset(dst, 0, HAPTIC_TEXT_LEN);
    if (src == NULL) {
        return;
    }

    for (uint8_t i = 0U; i < (HAPTIC_TEXT_LEN - 1U); i++) {
        if (src[i] == '\0') {
            break;
        }
        dst[i] = src[i];
    }
}

static void haptic_enqueue(const haptic_msg_t *msg)
{
    haptic_msg_t dropped;

    if (msg == NULL) {
        return;
    }

    if ((msg->type == HAPTIC_MSG_GESTURE_FEEDBACK) && (msg->arg0 == 0x04U)) {
        k_msgq_purge(&s_haptic_msgq);  /* emergency feedback jumps the queue */
    }

    if (k_msgq_put(&s_haptic_msgq, msg, K_NO_WAIT) == 0) {
        return;
    }

    /* Keep the newest user feedback if the queue is saturated. */
    (void)k_msgq_get(&s_haptic_msgq, &dropped, K_NO_WAIT);
    (void)k_msgq_put(&s_haptic_msgq, msg, K_NO_WAIT);
}

void haptic_post_oled_show(const char *line1, const char *line2)
{
    haptic_msg_t msg = {
        .type = HAPTIC_MSG_OLED_SHOW,
    };

    haptic_copy_text(msg.line1, line1);
    haptic_copy_text(msg.line2, line2);
    haptic_enqueue(&msg);
}

void haptic_post_buzzer_beep(uint8_t count, uint32_t pulse_ms, uint32_t gap_ms)
{
    haptic_msg_t msg = {
        .type        = HAPTIC_MSG_BUZZER_BEEP,
        .arg0        = count,
        .duration_ms = pulse_ms,
        .gap_ms      = gap_ms,
    };

    haptic_enqueue(&msg);
}

void haptic_post_motor_pulse(uint32_t duty_pct, uint32_t dur_ms)
{
    haptic_msg_t msg = {
        .type        = HAPTIC_MSG_MOTOR_PULSE,
        .arg0        = (uint8_t)((duty_pct > 100U) ? 100U : duty_pct),
        .duration_ms = dur_ms,
    };

    haptic_enqueue(&msg);
}

void haptic_post_caregiver_ack(uint8_t gesture_id)
{
    haptic_msg_t msg = {
        .type = HAPTIC_MSG_CAREGIVER_ACK,
        .arg0 = gesture_id,
    };

    haptic_enqueue(&msg);
}

void haptic_post_gesture_feedback(uint8_t gesture_id)
{
    haptic_msg_t msg = {
        .type = HAPTIC_MSG_GESTURE_FEEDBACK,
        .arg0 = gesture_id,
    };

    haptic_enqueue(&msg);
}

void haptic_post_led_ack(uint8_t count)
{
    haptic_msg_t msg = {
        .type = HAPTIC_MSG_LED_ACK,
        .arg0 = count,
    };

    haptic_enqueue(&msg);
}

void haptic_post_bad_cmd_diag(void)
{
    haptic_msg_t msg = {
        .type = HAPTIC_MSG_BAD_CMD_DIAG,
    };

    haptic_enqueue(&msg);
}

void haptic_post_oled_calib_show(uint8_t state, uint8_t idx, uint8_t mid,
                                 uint8_t rng, uint8_t pnk)
{
    haptic_msg_t msg = {
        .type = HAPTIC_MSG_OLED_CALIB,
        .arg0 = state,
        .arg1 = idx,
        .arg2 = mid,
        .arg3 = rng,
        .arg4 = pnk,
    };

    haptic_enqueue(&msg);
}

void haptic_init(void)
{
    if (device_is_ready(s_led.port)) {
        (void)gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE);
    }
    if (pwm_is_ready_dt(&s_buzzer)) {
        (void)pwm_set_dt(&s_buzzer, 0U, 0U);
    }
    if (pwm_is_ready_dt(&s_motor)) {
        motor_set_duty(0U);
    }
    if (device_is_ready(s_oled)) {
        s_oled_ready = true;
        (void)display_blanking_off(s_oled);
    }
}

void haptic_oled_show(const char *line1, const char *line2)
{
    oled_show4(line1, line2, NULL, NULL);
}

void haptic_oled_idle_tick(void)
{
    /* Citim now intern — tick_cmd() poate bloca 200-1000ms (gesture/ack feedback),
     * deci 'now' din main loop ar fi stale si ar cauza underflow uint32. */
    uint32_t now_ms = k_uptime_get_32();
    static bool s_in_idle;

    if (k_mutex_lock(&s_haptic_io_lock, K_NO_WAIT) != 0) {
        return;
    }

    if ((now_ms - s_oled_activity_time) >= 30000U) {  /* 30 s idle before reverting */
        if (!s_in_idle) {
            s_in_idle = true;
            (void)memset(s_fb, 0, sizeof(s_fb));
            oled_draw_str(0U, 0U, "GloveAssist");
            oled_draw_str(0U, 2U, "Activ");
            oled_flush_raw();
        }
    } else {
        s_in_idle = false;
    }

    k_mutex_unlock(&s_haptic_io_lock);
}

void haptic_oled_show4(const char *l0, const char *l2,
                       const char *l4, const char *l6)
{
    oled_show4(l0, l2, l4, l6);
}

/* Passive buzzer tone generated by hardware PWM. */
static void raw_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    uint32_t period_ns;

    if (!pwm_is_ready_dt(&s_buzzer) || (duration_ms == 0U)) {
        return;
    }

    if (freq_hz == 0U) {
        freq_hz = 2000U;
    }

    period_ns = 1000000000U / freq_hz;
    (void)pwm_set_dt(&s_buzzer, period_ns, period_ns / 2U);
    k_msleep(duration_ms);
    (void)pwm_set_dt(&s_buzzer, 0U, 0U);
}

void haptic_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    raw_tone(freq_hz, duration_ms);
}

void haptic_buzzer_beep(uint8_t count, uint32_t pulse_ms, uint32_t gap_ms)
{
    for (uint8_t i = 0U; i < count; i++) {
        haptic_tone(2000U, (pulse_ms > 0U) ? pulse_ms : 60U);
        if (i < (count - 1U)) {
            k_msleep(gap_ms);
        }
    }
}

void haptic_boot_melody(void)
{
    haptic_tone(1047U, 100U);   /* C6 */
    haptic_tone(1319U, 100U);   /* E6 */
    haptic_tone(1568U, 160U);   /* G6 */
}

void haptic_motor_duty(uint32_t duty_pct)
{
    motor_set_duty(duty_pct);
}

void haptic_motor_pulse(uint32_t duty_pct, uint32_t dur_ms)
{
    motor_set_duty(duty_pct);
    k_msleep(dur_ms);
    motor_set_duty(0U);
}

void haptic_caregiver_ack(uint8_t gesture_id)
{
    /*
     * OLED 4 linii:  "> OK PRIMIT! <" / eticheta gest specific / "Multumim!" / "Vine acum!"
     * Sunet: C6-E6-G6-C7 ascendent (confirmare vesela)
     * Motor: dit-dit-DIT (~570ms total, sub LINK_TIMEOUT_MS=3000ms)
     */
    static const char * const s_ack_labels[] = {
        "OK primit!",   /* GESTURE_NONE    (0) */
        "WATER: vine!", /* GESTURE_WATER   (1) */
        "WC: vine!",    /* GESTURE_WC      (2) */
        "FOOD: vine!",  /* GESTURE_FOOD    (3) */
        "HELP: OK!",    /* GESTURE_HELP    (4) */
        "Confirmat!",   /* GESTURE_CONFIRM (5) */
    };
    const char *label = (gesture_id < (uint8_t)ARRAY_SIZE(s_ack_labels))
                        ? s_ack_labels[gesture_id]
                        : "Inteles!";

    oled_show4("> OK PRIMIT! <", label, "Multumim!", "Vine acum!");

    haptic_tone(1047U, 80U);    /* C6 */
    haptic_tone(1319U, 80U);    /* E6 */
    haptic_tone(1568U, 80U);    /* G6 */
    haptic_tone(2093U, 220U);   /* C7 */

    /* dit-dit-DIT motor: scurt-scurt-lung (~650ms total) */
    motor_set_duty(90U); k_msleep(130U); motor_set_duty(0U); k_msleep(80U);
    motor_set_duty(90U); k_msleep(130U); motor_set_duty(0U); k_msleep(80U);
    motor_set_duty(100U); k_msleep(320U); motor_set_duty(0U);
}

void haptic_gesture_feedback(uint8_t gesture_id)
{
    /*
     * OLED: 4 linii — nume gest / traducere RO / indicator degete (O=indoit .=drept) / descriere
     * Sunet: melodie unica per gest
     * Motor: pornit concurent cu tonul, oprit dupa
     *
     *   WATER (1): G6 200ms + 1 impuls motor
     *   WC    (2): E6->A6 + 2 impulsuri motor
     *   FOOD  (3): C6-E6-G6 crescator + 3 impulsuri motor
     *   HELP  (4): 4 alarme rapide 2kHz + motor continuu ~710ms
     */
    switch ((uint8_t)gesture_id) {

    case 0x01U: /* GESTURE_WATER — 1 puls lung, calm */
        oled_show4("** WATER **", "Apa", "O . . .", "1 deget");
        motor_set_duty(90U);
        haptic_tone(1568U, 500U);       /* G6 — 500 ms */
        motor_set_duty(0U);
        k_msleep(100U);
        motor_set_duty(90U); k_msleep(400U); motor_set_duty(0U);
        break;

    case 0x02U: /* GESTURE_WC — 2 pulsuri clare */
        oled_show4("** WC **", "Toaleta", "O O . .", "2 degete");
        motor_set_duty(90U);
        haptic_tone(1319U, 100U);       /* E6 */
        k_msleep(40U);
        haptic_tone(1760U, 180U);       /* A6 */
        motor_set_duty(0U);
        k_msleep(100U);
        motor_set_duty(90U); k_msleep(200U); motor_set_duty(0U); k_msleep(100U);
        motor_set_duty(90U); k_msleep(200U); motor_set_duty(0U);
        break;

    case 0x03U: /* GESTURE_FOOD — 3 pulsuri crescatoare */
        oled_show4("** FOOD **", "Mancare", "O O O .", "3 degete");
        motor_set_duty(90U);
        haptic_tone(1047U, 80U);        /* C6 */
        haptic_tone(1319U, 80U);        /* E6 */
        haptic_tone(1568U, 150U);       /* G6 */
        motor_set_duty(0U);
        k_msleep(80U);
        motor_set_duty(90U); k_msleep(150U); motor_set_duty(0U); k_msleep(80U);
        motor_set_duty(90U); k_msleep(150U); motor_set_duty(0U); k_msleep(80U);
        motor_set_duty(90U); k_msleep(250U); motor_set_duty(0U);
        break;

    case 0x04U: /* GESTURE_HELP — motor continuu + alarme rapide */
        oled_show4("!!! SOS !!!", "Ajutor!", "O O O O", "!! URGENTA !!");
        motor_set_duty(100U);
        /* 4x(80ms ton + 60ms pauza) + 200ms hold motor ≈ 760ms total */
        raw_tone(2000U, 80U); k_msleep(60U);
        raw_tone(2000U, 80U); k_msleep(60U);
        raw_tone(2000U, 80U); k_msleep(60U);
        raw_tone(2000U, 80U);
        k_msleep(200U);
        motor_set_duty(0U);
        break;

    default:
        break;
    }
}

/* Writes "X:NNN Y:NNN\0" into buf (must be >= 12 bytes). */
static void fmt_pair(char *buf, char l1, uint8_t v1, char l2, uint8_t v2)
{
    buf[0] = l1; buf[1] = ':';
    buf[2] = (char)('0' + v1 / 100U);
    buf[3] = (char)('0' + (v1 / 10U) % 10U);
    buf[4] = (char)('0' + v1 % 10U);
    buf[5] = ' ';
    buf[6] = l2; buf[7] = ':';
    buf[8] = (char)('0' + v2 / 100U);
    buf[9] = (char)('0' + (v2 / 10U) % 10U);
    buf[10] = (char)('0' + v2 % 10U);
    buf[11] = '\0';
}

void haptic_oled_calib_show(uint8_t state, uint8_t idx, uint8_t mid,
                             uint8_t rng, uint8_t pnk)
{
    /*
     * state = phase * 4 + countdown_step
     *   phase:          0=NONE 1=WATER 2=WC 3=FOOD 4=HELP
     *   countdown_step: 0=Gata:3  1=Gata:2  2=Gata:1  3=TINE!
     * Range: 0..19 (5 gestures × 4 steps)
     */
    static const char *const s_labels[] = {
        "NONE", "WATER", "WC", "FOOD", "HELP"
    };
    static const char *const s_cnt[] = {
        "Gata: 3", "Gata: 2", "Gata: 1", "TINE!"
    };

    uint8_t phase = state / 4U;
    uint8_t step  = state % 4U;

    const char *label = (phase < 5U) ? s_labels[phase] : "CALIB";
    const char *cnt   = s_cnt[(step < 4U) ? step : 3U];

    char line_im[12];
    char line_rp[12];

    fmt_pair(line_im, 'I', idx, 'M', mid);
    fmt_pair(line_rp, 'R', rng, 'P', pnk);

    (void)memset(s_fb, 0, sizeof(s_fb));
    oled_draw_str(0U, 0U, label);    /* row 0: NONE/WATER/WC/FOOD/HELP */
    oled_draw_str(0U, 2U, cnt);      /* row 2: Gata:3 / TINE!          */
    oled_draw_str(0U, 4U, line_im);  /* row 4: I:197 M:195             */
    oled_draw_str(0U, 6U, line_rp);  /* row 6: R:191 P:188             */
    oled_flush();
}

void haptic_led_set(bool on)
{
    if (device_is_ready(s_led.port)) {
        (void)gpio_pin_set_dt(&s_led, on ? 1 : 0);
    }
}

void haptic_led_toggle(void)
{
    if (device_is_ready(s_led.port)) {
        (void)gpio_pin_toggle_dt(&s_led);
    }
}

static void haptic_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (1) {
        haptic_msg_t msg;

        if (k_msgq_get(&s_haptic_msgq, &msg, K_FOREVER) != 0) {
            continue;
        }

        k_mutex_lock(&s_haptic_io_lock, K_FOREVER);

        switch (msg.type) {
        case HAPTIC_MSG_OLED_SHOW:
            haptic_oled_show(msg.line1, msg.line2);
            break;

        case HAPTIC_MSG_BUZZER_BEEP:
            haptic_buzzer_beep(msg.arg0, msg.duration_ms, msg.gap_ms);
            break;

        case HAPTIC_MSG_MOTOR_PULSE:
            haptic_motor_pulse(msg.arg0, msg.duration_ms);
            break;

        case HAPTIC_MSG_CAREGIVER_ACK:
            haptic_caregiver_ack(msg.arg0);
            break;

        case HAPTIC_MSG_GESTURE_FEEDBACK:
            haptic_gesture_feedback(msg.arg0);
            break;

        case HAPTIC_MSG_LED_ACK:
            for (uint8_t i = 0U; i < msg.arg0; i++) {
                haptic_led_set(true);
                k_msleep(80U);
                haptic_led_set(false);
                k_msleep(80U);
            }
            break;

        case HAPTIC_MSG_BAD_CMD_DIAG:
            haptic_oled_show("BAD CMD", "AUTH/SOF");
            haptic_led_set(true);
            k_msleep(150U);
            haptic_led_set(false);
            break;

        case HAPTIC_MSG_OLED_CALIB:
            haptic_oled_calib_show(msg.arg0, msg.arg1, msg.arg2,
                                   msg.arg3, msg.arg4);
            break;

        default:
            break;
        }

        k_mutex_unlock(&s_haptic_io_lock);
    }
}
