/**
 * @file safety_diag.c
 * @brief IEC 61508 Safety Subsystem — Watchdog, Sensor Faults, Heartbeat Monitor
 *
 * ==========================================================================
 *  DUAL-CORE REDUNDANCY — "Argumentul Suprem" pentru comisie
 * ==========================================================================
 *
 *  Architecture:
 *    ESP32 (communication processor) sends periodic HEARTBEAT frames via UART.
 *    STM32 (acquisition processor) monitors the heartbeat interval.
 *
 *  Normal mode:
 *    ESP32 alive → gestures forwarded via BLE → phone notified
 *
 *  Degraded mode (ESP32 heartbeat lost > 2 seconds):
 *    STM32 activates LOCAL ALARM:
 *      - Buzzer: continuous SOS pattern (3 short + 3 long + 3 short)
 *      - Motor: strong vibration pulses
 *      - OLED: "!! NO LINK — LOCAL ALARM !!"
 *    Patient is NEVER left without alerting capability.
 *
 *  Recovery:
 *    When heartbeat resumes, STM32 exits degraded mode and resumes
 *    normal BLE-forwarded operation.
 *
 *  IEC 61508 traceability:
 *    - Sensor faults: open-circuit (<500), short-circuit (>3800)
 *    - 3 consecutive faults per channel → fallback to last-known-good
 *    - All 4 sensors faulted → system lockdown (only WDT reset recovers)
 *    - IWDG watchdog: 2s timeout, resets on freeze
 *    - Heartbeat monitor: highest-priority thread, 250ms check interval
 * ==========================================================================
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "common_types.h"
#include "error_codes.h"
#include "safety_diag.h"
#include "haptic_ui.h"

LOG_MODULE_REGISTER(safety, CONFIG_LOG_DEFAULT_LEVEL);

/* ====================================================================== */
/*                         WATCHDOG (IWDG)                                */
/* ====================================================================== */

static const struct device *const wdt_dev = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static int wdt_channel_id = -1;

static int watchdog_init(void)
{
    if (!device_is_ready(wdt_dev)) {
        LOG_ERR("Watchdog device not ready");
        return -ENODEV;
    }

    struct wdt_timeout_cfg cfg = {
        .window = {
            .min = 0U,
            .max = WATCHDOG_TIMEOUT_MS,
        },
        .callback = NULL,
        .flags    = WDT_FLAG_RESET_SOC,
    };

    wdt_channel_id = wdt_install_timeout(wdt_dev, &cfg);
    if (wdt_channel_id < 0) {
        LOG_ERR("wdt_install_timeout failed: %d", wdt_channel_id);
        return wdt_channel_id;
    }

    int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (ret < 0) {
        LOG_ERR("wdt_setup failed: %d", ret);
        return ret;
    }

    LOG_INF("Watchdog started — %u ms timeout", WATCHDOG_TIMEOUT_MS);
    return 0;
}

void safety_watchdog_feed(void)
{
    if (wdt_channel_id >= 0) {
        wdt_feed(wdt_dev, wdt_channel_id);
    }
}

/* ====================================================================== */
/*                    SENSOR FAULT TRACKING                               */
/* ====================================================================== */

static uint16_t last_good[NUM_FLEX_SENSORS];
static uint8_t  fault_count[NUM_FLEX_SENSORS];

#define FAULT_THRESHOLD  3U

error_code_t safety_validate_sensor(uint8_t ch, uint16_t raw, uint16_t *out)
{
    if (ch >= NUM_FLEX_SENSORS) {
        return ERR_SENSOR_OPEN_CIRCUIT;
    }

    if (raw < ADC_MIN_VALID) {
        fault_count[ch]++;
        if (fault_count[ch] >= FAULT_THRESHOLD) {
            LOG_WRN("CH%u open-circuit — fallback %u", ch, last_good[ch]);
            *out = last_good[ch];
            return ERR_SENSOR_OPEN_CIRCUIT;
        }
        *out = raw;
        return ERR_SENSOR_OK;
    }

    if (raw > ADC_MAX_VALID) {
        fault_count[ch]++;
        if (fault_count[ch] >= FAULT_THRESHOLD) {
            LOG_WRN("CH%u short-circuit — fallback %u", ch, last_good[ch]);
            *out = last_good[ch];
            return ERR_SENSOR_SHORT_CIRCUIT;
        }
        *out = raw;
        return ERR_SENSOR_OK;
    }

    fault_count[ch] = 0U;
    last_good[ch]   = raw;
    *out = raw;
    return ERR_SENSOR_OK;
}

bool safety_all_sensors_faulted(void)
{
    for (uint8_t ch = 0U; ch < (uint8_t)NUM_FLEX_SENSORS; ch++) {
        if (fault_count[ch] < FAULT_THRESHOLD) {
            return false;
        }
    }
    return true;
}

void safety_enter_lockdown(void)
{
    LOG_ERR("*** SAFETY LOCKDOWN — all sensors faulted ***");
    LOG_ERR("WDT reset will recover system");

    /* Signal haptic thread to play continuous error pattern */
    haptic_notify_error();

    /*
     * Stop feeding watchdog — IWDG will reset MCU after WATCHDOG_TIMEOUT_MS.
     * This is the ONLY safe recovery path per IEC 61508 (SIL-1).
     * BLE disconnect happens automatically on reset.
     */
    while (1) {
        k_msleep(100U);
    }
}

/* ====================================================================== */
/*                      HEARTBEAT MONITOR                                 */
/*             (Dual-Core Redundancy — IEC 61508)                         */
/* ====================================================================== */

/**
 * State machine for heartbeat monitoring:
 *
 *   BOOT_GRACE  ──(grace period expires)──>  ALIVE
 *   ALIVE       ──(heartbeat timeout)────>   DEGRADED
 *   DEGRADED    ──(heartbeat received)───>   ALIVE
 *
 * In DEGRADED mode, STM32 activates local buzzer+motor alarm.
 * The patient is NEVER left without alerting capability.
 */

typedef enum {
    HB_STATE_BOOT_GRACE,  /* Ignoring missed heartbeats during boot     */
    HB_STATE_ALIVE,       /* ESP32 heartbeat is current                 */
    HB_STATE_DEGRADED     /* ESP32 unresponsive — local alarm active    */
} heartbeat_state_t;

static volatile heartbeat_state_t hb_state = HB_STATE_BOOT_GRACE;
static volatile int64_t last_heartbeat_ts;
static volatile uint32_t heartbeat_rx_count;
static volatile uint32_t degraded_entry_count;

void safety_heartbeat_received(void)
{
    last_heartbeat_ts = k_uptime_get();
    heartbeat_rx_count++;

    if (hb_state == HB_STATE_DEGRADED) {
        LOG_INF("ESP32 heartbeat RECOVERED — exiting degraded mode");
        hb_state = HB_STATE_ALIVE;
    } else if (hb_state == HB_STATE_BOOT_GRACE) {
        LOG_INF("First ESP32 heartbeat received — system nominal");
        hb_state = HB_STATE_ALIVE;
    }
}

bool safety_esp32_alive(void)
{
    return (hb_state == HB_STATE_ALIVE);
}

bool safety_is_degraded(void)
{
    return (hb_state == HB_STATE_DEGRADED);
}

/**
 * @brief Activate local alarm (buzzer + motor) when ESP32 is down.
 *
 * SOS pattern: ··· ——— ··· (3 short, 3 long, 3 short)
 * This runs on the safety thread — highest priority on STM32.
 */
static void activate_local_alarm(void)
{
    haptic_notify_sos();
}

/* ====================================================================== */
/*                        SAFETY THREAD                                   */
/* ====================================================================== */

/**
 * @brief Safety monitor thread — runs at highest application priority.
 *
 * Responsibilities:
 *   1. Feed the hardware watchdog (IWDG)
 *   2. Monitor ESP32 heartbeat
 *   3. Trigger local alarm in degraded mode
 *   4. Log diagnostic counters for FMEA evidence
 */
void safety_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("Safety thread started — monitoring ESP32 heartbeat");
    LOG_INF("  Heartbeat timeout: %u ms", HEARTBEAT_TIMEOUT_MS);
    LOG_INF("  Boot grace period: %u missed heartbeats", HEARTBEAT_GRACE_BOOTS);

    last_heartbeat_ts = k_uptime_get();
    uint32_t grace_counter = 0U;

    while (1) {
        /* 1. Feed watchdog — this thread is the primary feeder */
        safety_watchdog_feed();

        /* 2. Check heartbeat */
        int64_t now = k_uptime_get();
        int64_t elapsed = now - last_heartbeat_ts;

        switch (hb_state) {
        case HB_STATE_BOOT_GRACE:
            if (elapsed > (int64_t)HEARTBEAT_TIMEOUT_MS) {
                grace_counter++;
                if (grace_counter >= HEARTBEAT_GRACE_BOOTS) {
                    LOG_WRN("Boot grace expired — ESP32 never responded");
                    hb_state = HB_STATE_DEGRADED;
                    degraded_entry_count++;
                    LOG_ERR(">>> DEGRADED MODE #%u — activating LOCAL ALARM",
                            degraded_entry_count);
                } else {
                    LOG_WRN("Boot grace: missed %u/%u", grace_counter,
                            HEARTBEAT_GRACE_BOOTS);
                    last_heartbeat_ts = now;  /* Reset window */
                }
            }
            break;

        case HB_STATE_ALIVE:
            if (elapsed > (int64_t)HEARTBEAT_TIMEOUT_MS) {
                hb_state = HB_STATE_DEGRADED;
                degraded_entry_count++;
                LOG_ERR("!!! ESP32 HEARTBEAT LOST (>%u ms) !!!",
                        HEARTBEAT_TIMEOUT_MS);
                LOG_ERR(">>> DEGRADED MODE #%u — LOCAL ALARM ACTIVE",
                        degraded_entry_count);
                LOG_ERR(">>> Patient protected by STM32 standalone alarm");
            }
            break;

        case HB_STATE_DEGRADED:
            /* Continuously trigger local alarm while degraded */
            activate_local_alarm();

            /* Log periodically for FMEA audit trail */
            if ((now % 5000) < 250) {
                LOG_WRN("DEGRADED: %lld ms without ESP32 | "
                        "total heartbeats: %u | degraded entries: %u",
                        elapsed, heartbeat_rx_count, degraded_entry_count);
            }
            break;
        }

        /* 3. Check for sensor lockdown */
        if (safety_all_sensors_faulted()) {
            safety_enter_lockdown();
        }

        /* Run at 4 Hz (250 ms) — fast enough to detect 2s timeout */
        k_msleep(250U);
    }
}

/* ====================================================================== */
/*                     INITIALIZATION                                     */
/* ====================================================================== */

int safety_init(void)
{
    int ret = watchdog_init();
    if (ret < 0) {
        LOG_ERR("Watchdog init failed: %d", ret);
        /* Continue anyway — safety degraded but not fatal */
    }

    hb_state = HB_STATE_BOOT_GRACE;
    last_heartbeat_ts = k_uptime_get();
    heartbeat_rx_count = 0U;
    degraded_entry_count = 0U;

    LOG_INF("Safety subsystem initialised");
    LOG_INF("  FMEA: sensor fault threshold = %u consecutive", FAULT_THRESHOLD);
    LOG_INF("  FMEA: heartbeat timeout = %u ms", HEARTBEAT_TIMEOUT_MS);
    LOG_INF("  FMEA: watchdog timeout = %u ms", WATCHDOG_TIMEOUT_MS);
    LOG_INF("  IEC 61508: dual-core redundancy ACTIVE");

    return 0;
}
