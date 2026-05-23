/**
 * @file haptic_ui.c
 * @brief Haptic feedback (motor + buzzer) and OLED display driver
 *
 * Motor:  ULN2003 low-side driver, software PWM on IN1
 * Buzzer: passive buzzer, software PWM (GPIO toggle at freq_hz)
 * OLED:   SSD1306 128×64 via I2C
 */

#include <zephyr/kernel.h>
/* Motor folosește software PWM (GPIO), nu hardware PWM */
/* #include <zephyr/drivers/pwm.h> */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "app_config.h"
#include "common_types.h"
#include "haptic_ui.h"

LOG_MODULE_REGISTER(haptic_ui, CONFIG_LOG_DEFAULT_LEVEL);

/* ---------- DeviceTree bindings ---------- */
/* Motor folosește software PWM (GPIO PB6) */
static const struct gpio_dt_spec motor_gpio = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 6,
	.dt_flags = GPIO_ACTIVE_HIGH
};
static const struct gpio_dt_spec buzzer_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(buzzergpio), gpios);

static const struct device *const oled_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c2));
static bool oled_ready;
static uint8_t oled_i2c_addr;
static uint8_t oled_col_offset;
static u8_t last_gesture_mask;

/* ---------- Event flags for haptic patterns ---------- */
K_EVENT_DEFINE(haptic_events);
#define EVT_GESTURE_ACK    BIT(0)
#define EVT_MESSAGE_RX     BIT(1)
#define EVT_ERROR          BIT(2)
#define EVT_SOS_ALARM      BIT(3)   /* Degraded mode: ESP32 down */
#define EVT_ACK_RECEIVED   BIT(4)   /* Phone confirmed "inteles" */

/* Gesture id shared between caller and haptic thread (for per-gesture beeps) */
static atomic_t s_pending_gesture_id;

/* ---------- OLED layout (lightweight, no font dependency) ---------- */
#define OLED_WIDTH_PX         128U
#define OLED_HEIGHT_PX         64U
#define OLED_PAGE_COUNT        (OLED_HEIGHT_PX / 8U)
#define OLED_BAR_COUNT          4U
#define OLED_BAR_WIDTH         20U
#define OLED_BAR_GAP           10U
#define OLED_BAR_X0             9U
#define OLED_ACTIVE_BAR_PAGES   6U
#define OLED_IDLE_BAR_PAGES     1U

/* ---------- Motor control via software PWM (GPIO toggle) ---------- */
/* PWM software: 100Hz (10ms period), duty 0-100% */
static void motor_pwm_soft(u8_t duty_percent, u32_t duration_ms)
{
    if (duty_percent == 0U) {
        gpio_pin_set_dt(&motor_gpio, 0);
        k_msleep(duration_ms);
        return;
    }
    
    if (duty_percent >= 100U) {
        gpio_pin_set_dt(&motor_gpio, 1);
        k_msleep(duration_ms);
        return;
    }
    
    u32_t period_ms = 10U;  /* 100Hz */
    u32_t on_time_ms = (period_ms * duty_percent) / 100U;
    u32_t off_time_ms = period_ms - on_time_ms;
    u32_t cycles = duration_ms / period_ms;
    
    for (u32_t i = 0U; i < cycles; i++) {
        gpio_pin_set_dt(&motor_gpio, 1);
        k_msleep(on_time_ms);
        gpio_pin_set_dt(&motor_gpio, 0);
        k_msleep(off_time_ms);
    }
}

static void motor_pulse(u32_t duration_ms)
{
    motor_pwm_soft(50U, duration_ms);  /* 50% duty pentru pulse normal */
}

/* ---------- Buzzer patterns ---------- */
/*
 * Software PWM pentru buzzer pasiv: toggle GPIO la freq_hz timp de duration_ms ms.
 * k_busy_wait este intenționat — perioadele sunt de ordinul sutelor de µs.
 */
static void buzzer_beep(u32_t freq_hz, u32_t duration_ms)
{
    if (freq_hz == 0U || duration_ms == 0U) {
        return;
    }

    u32_t half_period_us = 500000U / freq_hz; /* µs la jumătate de perioadă */
    u32_t total_us       = duration_ms * 1000U;
    u32_t elapsed        = 0U;

    while (elapsed < total_us) {
        gpio_pin_set_dt(&buzzer_gpio, 1);
        k_busy_wait(half_period_us);
        gpio_pin_set_dt(&buzzer_gpio, 0);
        k_busy_wait(half_period_us);
        elapsed += half_period_us * 2U;
    }
}

/* ---------- SOS pattern (degraded mode alarm) ---------- */
static void play_sos_pattern(void)
{
    /* SOS: ··· ——— ··· */
    /* 3 short beeps */
    for (u8_t i = 0U; i < 3U; i++) {
        motor_pulse(100U);
        buzzer_beep(2500U, 100U);
        k_msleep(50U);
    }
    k_msleep(150U);
    /* 3 long beeps */
    for (u8_t i = 0U; i < 3U; i++) {
        motor_pulse(HAPTIC_SOS_PULSE_MS);
        buzzer_beep(2500U, HAPTIC_SOS_PULSE_MS);
        k_msleep(100U);
    }
    k_msleep(150U);
    /* 3 short beeps */
    for (u8_t i = 0U; i < 3U; i++) {
        motor_pulse(100U);
        buzzer_beep(2500U, 100U);
        k_msleep(50U);
    }
}

/* ---------- OLED helpers ---------- */
static int oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00U, cmd };

    return i2c_write(oled_i2c_dev, buf, sizeof(buf), oled_i2c_addr);
}

static int oled_data(const uint8_t *data, size_t len)
{
    uint8_t buf[1U + OLED_WIDTH_PX];

    if (len > OLED_WIDTH_PX) {
        return -EINVAL;
    }

    buf[0] = 0x40U;
    (void)memcpy(&buf[1], data, len);

    return i2c_write(oled_i2c_dev, buf, (uint32_t)(len + 1U), oled_i2c_addr);
}

static int oled_send_cmds(uint8_t addr, const uint8_t *cmds, size_t count)
{
    oled_i2c_addr = addr;
    for (size_t i = 0U; i < count; i++) {
        int rc = oled_cmd(cmds[i]);
        if (rc < 0) {
            return rc;
        }
    }

    return 0;
}

static int oled_init_ssd1306_at(uint8_t addr)
{
    static const uint8_t init_seq[] = {
        0xAEU,       /* display off */
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0x8DU, 0x14U,
        0x20U, 0x02U, /* page addressing, same as the working OLED test */
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0xCFU,
        0xD9U, 0xF1U,
        0xDBU, 0x40U,
        0xA4U,
        0xA6U,
        0xAFU        /* display on */
    };

    oled_col_offset = 0U;
    return oled_send_cmds(addr, init_seq, sizeof(init_seq));
}

static int oled_init_sh1106_at(uint8_t addr)
{
    static const uint8_t init_seq[] = {
        0xAEU,
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0xADU, 0x8BU,
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0x80U,
        0xD9U, 0x22U,
        0xDBU, 0x35U,
        0xA4U,
        0xA6U,
        0xAFU,
    };

    oled_col_offset = 2U;
    return oled_send_cmds(addr, init_seq, sizeof(init_seq));
}

static int oled_raw_init(void)
{
    if (!device_is_ready(oled_i2c_dev)) {
        LOG_WRN("OLED I2C2 bus not ready");
        return -ENODEV;
    }

    static const uint8_t addrs[] = { 0x3CU, 0x3DU };

    for (size_t i = 0U; i < ARRAY_SIZE(addrs); i++) {
        uint8_t addr = addrs[i];

        if (oled_init_ssd1306_at(addr) == 0) {
            LOG_INF("OLED SSD1306 init OK at 0x%02x", addr);
            return 0;
        }

        if (oled_init_sh1106_at(addr) == 0) {
            LOG_INF("OLED SH1106 init OK at 0x%02x", addr);
            return 0;
        }
    }

    oled_i2c_addr = 0U;
    oled_col_offset = 0U;
    LOG_WRN("OLED not found at 0x3c/0x3d");
    return -ENODEV;
}

static void oled_write_page(u8_t page, const uint8_t line[OLED_WIDTH_PX])
{
    if (!oled_ready || oled_i2c_addr == 0U) {
        return;
    }

    (void)oled_cmd((uint8_t)(0xB0U | (page & 0x07U)));
    (void)oled_cmd((uint8_t)(0x00U + (oled_col_offset & 0x0FU)));
    (void)oled_cmd((uint8_t)(0x10U + ((oled_col_offset >> 4U) & 0x0FU)));
    (void)oled_data(line, OLED_WIDTH_PX);
}

static u8_t gesture_to_mask(u8_t gesture_id)
{
    switch (gesture_id) {
    case GESTURE_INDEX:
        return (u8_t)(1U << FINGER_INDEX);
    case GESTURE_MIDDLE:
        return (u8_t)(1U << FINGER_MIDDLE);
    case GESTURE_RING:
        return (u8_t)(1U << FINGER_RING);
    case GESTURE_PINKY:
        return (u8_t)(1U << FINGER_PINKY);
    case GESTURE_FIST:
        return 0x0FU;
    case GESTURE_HELP:
        return (u8_t)((1U << FINGER_RING) | (1U << FINGER_PINKY));
    case GESTURE_NONE:
    default:
        return 0U;
    }
}

static void oled_draw_bars(u8_t active_mask, bool alarm)
{
    uint8_t line[OLED_WIDTH_PX];

    if (!oled_ready) {
        return;
    }

    for (u8_t page = 0U; page < OLED_PAGE_COUNT; page++) {
        (void)memset(line, 0x00, sizeof(line));

        for (u8_t finger = 0U; finger < OLED_BAR_COUNT; finger++) {
            bool active = ((active_mask & (u8_t)(1U << finger)) != 0U);
            u8_t bar_pages = active ? OLED_ACTIVE_BAR_PAGES : OLED_IDLE_BAR_PAGES;
            u8_t first_page_on = (u8_t)(OLED_PAGE_COUNT - bar_pages);

            if (page >= first_page_on) {
                u8_t x0 = (u8_t)(OLED_BAR_X0 + finger * (OLED_BAR_WIDTH + OLED_BAR_GAP));
                u8_t x1 = (u8_t)(x0 + OLED_BAR_WIDTH);

                if (x1 > OLED_WIDTH_PX) {
                    x1 = OLED_WIDTH_PX;
                }

                for (u8_t x = x0; x < x1; x++) {
                    line[x] = 0xFFU;
                }
            }
        }

        /* Bottom baseline stays visible in idle state too. */
        if (page == (OLED_PAGE_COUNT - 1U)) {
            for (u8_t x = 0U; x < OLED_WIDTH_PX; x++) {
                line[x] = (uint8_t)(line[x] | 0x03U);
            }
        }

        if (alarm) {
            line[0] = 0xFFU;
            line[1] = 0xFFU;
            line[OLED_WIDTH_PX - 2U] = 0xFFU;
            line[OLED_WIDTH_PX - 1U] = 0xFFU;

            if ((page == 0U) || (page == (OLED_PAGE_COUNT - 1U))) {
                (void)memset(line, 0xFF, sizeof(line));
            }
        }

        oled_write_page(page, line);
    }
}

static void oled_draw_boot_pattern(void)
{
    uint8_t line[OLED_WIDTH_PX];

    if (!oled_ready) {
        return;
    }

    memset(line, 0xFF, sizeof(line));

    for (u8_t page = 0U; page < OLED_PAGE_COUNT; page++) {
        oled_write_page(page, line);
        k_msleep(30U);
    }
}

static int oled_show_gesture(u8_t gesture_id)
{
    u8_t mask = gesture_to_mask(gesture_id);

    if (gesture_id > GESTURE_MAX_ID) {
        LOG_WRN("GESTURE invalid id=%u", gesture_id);
    }

    last_gesture_mask = mask;
    LOG_INF("GESTURE id=%u mask=0x%02x", gesture_id, mask);
    oled_draw_bars(mask, false);

    return 0;
}

/**
 * @brief Display "OK" pattern on OLED: 3 quick full-screen flashes.
 *
 * Used when phone confirms "inteles". No font needed — visible blink
 * pattern distinguishes ACK from normal gesture bar display.
 */
static void oled_show_ok(void)
{
    uint8_t line_full[OLED_WIDTH_PX];
    uint8_t line_empty[OLED_WIDTH_PX];
    if (!oled_ready) {
        return;
    }

    (void)memset(line_full,  0xFFU, sizeof(line_full));
    (void)memset(line_empty, 0x00U, sizeof(line_empty));

    /* 3 full-screen blink flashes = "OK received" */
    for (u8_t flash = 0U; flash < 3U; flash++) {
        /* Fill screen */
        for (u8_t page = 0U; page < OLED_PAGE_COUNT; page++) {
            oled_write_page(page, line_full);
        }
        k_msleep(120U);
        /* Clear screen */
        for (u8_t page = 0U; page < OLED_PAGE_COUNT; page++) {
            oled_write_page(page, line_empty);
        }
        k_msleep(80U);
    }

    /* Restore last gesture display */
    oled_draw_bars(last_gesture_mask, false);
}

/* ---------- Haptic thread ---------- */
void haptic_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Haptic/UI thread started");

    if (!device_is_ready(motor_gpio.port)) {
        LOG_ERR("Motor GPIO device not ready");
    } else if (gpio_pin_configure_dt(&motor_gpio, GPIO_OUTPUT_INACTIVE) != 0) {
        LOG_ERR("Motor GPIO configure failed");
    }
    if (!device_is_ready(buzzer_gpio.port)) {
        LOG_ERR("Buzzer GPIO device not ready");
    } else if (gpio_pin_configure_dt(&buzzer_gpio, GPIO_OUTPUT_INACTIVE) != 0) {
        LOG_ERR("Buzzer GPIO configure failed");
    }

    k_msleep(2000U);
    if (oled_raw_init() == 0) {
        oled_ready = true;
        oled_draw_boot_pattern();
        oled_draw_bars(0U, false);
        LOG_INF("OLED boot pattern shown");
    } else {
        oled_ready = false;
        LOG_WRN("OLED init failed");
    }

    while (1) {
        uint32_t events = k_event_wait(&haptic_events,
                                       EVT_GESTURE_ACK | EVT_MESSAGE_RX |
                                       EVT_ERROR | EVT_SOS_ALARM | EVT_ACK_RECEIVED,
                                       false, K_MSEC(OLED_REFRESH_MS));

        if (events & EVT_SOS_ALARM) {
            k_event_clear(&haptic_events, EVT_SOS_ALARM);
            play_sos_pattern();
        }

        if (events & EVT_GESTURE_ACK) {
            k_event_clear(&haptic_events, EVT_GESTURE_ACK);

            /* Per-gesture beep count: NONE=0, INDEX=1, MIDDLE=2, RING=3,
             * PINKY=4, FIST=5 (long), HELP=2 rapid (SOS-like) */
            static const uint8_t k_beep_count[] = {
                [GESTURE_NONE]   = 0U,
                [GESTURE_INDEX]  = 1U,
                [GESTURE_MIDDLE] = 2U,
                [GESTURE_RING]   = 3U,
                [GESTURE_PINKY]  = 4U,
                [GESTURE_FIST]   = 5U,
                [GESTURE_HELP]   = 2U,
            };

            u8_t gid = (u8_t)atomic_get(&s_pending_gesture_id);
            uint8_t beeps = (gid <= (u8_t)GESTURE_MAX_ID) ?
                            k_beep_count[gid] : 0U;

            /* Short motor pulse for tactile confirmation */
            motor_pulse(HAPTIC_GESTURE_PULSE_MS);

            /* N auditory beeps per gesture */
            for (u8_t b = 0U; b < beeps; b++) {
                buzzer_beep(2000U, 80U);
                k_msleep(100U);
            }
        }

        if (events & EVT_ACK_RECEIVED) {
            k_event_clear(&haptic_events, EVT_ACK_RECEIVED);
            /* "inteles" from phone: OLED flash + 2 ascending beeps + motor */
            oled_show_ok();
            motor_pulse(80U);
            k_msleep(60U);
            motor_pulse(80U);
            buzzer_beep(1200U, 80U);
            k_msleep(80U);
            buzzer_beep(2400U, 120U);
        }

        if (events & EVT_MESSAGE_RX) {
            k_event_clear(&haptic_events, EVT_MESSAGE_RX);
            motor_pulse(HAPTIC_MSG_PULSE_MS);
            k_msleep(100U);
            motor_pulse(HAPTIC_MSG_PULSE_MS);
        }

        if (events & EVT_ERROR) {
            k_event_clear(&haptic_events, EVT_ERROR);
            for (u8_t i = 0U; i < 3U; i++) {
                motor_pulse(HAPTIC_ERROR_PULSE_MS);
                buzzer_beep(1000U, HAPTIC_ERROR_PULSE_MS);
                k_msleep(50U);
            }
        }
    }
}

/* ---------- Public API ---------- */
void haptic_notify_gesture(u8_t gesture_id)
{
    atomic_set(&s_pending_gesture_id, (atomic_val_t)gesture_id);
    oled_show_gesture(gesture_id);
    k_event_post(&haptic_events, EVT_GESTURE_ACK);
}

void haptic_notify_message(void)
{
    k_event_post(&haptic_events, EVT_MESSAGE_RX);
}

void haptic_notify_error(void)
{
    oled_draw_bars(last_gesture_mask, true);
    k_event_post(&haptic_events, EVT_ERROR);
}

void haptic_notify_sos(void)
{
    oled_draw_bars(0x0FU, true);
    k_event_post(&haptic_events, EVT_SOS_ALARM);
}

void haptic_notify_ack_received(void)
{
    k_event_post(&haptic_events, EVT_ACK_RECEIVED);
}
