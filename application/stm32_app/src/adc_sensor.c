/*
 * adc_sensor.c — ADC1 + DMA1 circular acquisition for 4 flex sensors.
 *
 * PA0 = index finger, PA1 = middle, PA2 = ring, PA3 = pinky.
 * 12-bit resolution, internal reference, ~239.5-cycle sample time.
 * DMA1 Channel1 in circular mode writes continuously to s_adc[4].
 */

#include <zephyr/kernel.h>
#include <soc.h>
#include <string.h>
#include "adc_sensor.h"

static volatile uint16_t s_adc[ADC_NUM_CHANNELS];

static uint8_t s_bad_count[ADC_NUM_CHANNELS];
static uint8_t s_good_count[ADC_NUM_CHANNELS];
static adc_sensor_fault_status_t s_fault;

void adc_sensor_init(void)
{
    /* ── Clocks ──────────────────────────────────────────────────── */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_ADC1EN;
    RCC->AHBENR  |= RCC_AHBENR_DMA1EN;

    /* ADC prescaler: /4  →  8 MHz HSI / 4 = 2 MHz (< 14 MHz limit) */
    RCC->CFGR = (RCC->CFGR & ~(3U << 14U)) | (2U << 14U);

    /* ── PA0..PA3 → analog (CRL bits [15:0] = 0) ─────────────────── */
    GPIOA->CRL &= ~0x0000FFFFu;

    /* ── DMA1 Channel1 for ADC1 DR ────────────────────────────────── */
    DMA1_Channel1->CCR   = 0U;
    DMA1_Channel1->CPAR  = (uint32_t)&ADC1->DR;
    DMA1_Channel1->CMAR  = (uint32_t)s_adc;
    DMA1_Channel1->CNDTR = ADC_NUM_CHANNELS;
    DMA1_Channel1->CCR   = DMA_CCR_CIRC    /* circular mode          */
                         | DMA_CCR_MINC    /* memory increment       */
                         | DMA_CCR_MSIZE_0 /* 16-bit memory transfers*/
                         | DMA_CCR_PSIZE_0 /* 16-bit periph transfers*/
                         | DMA_CCR_EN;

    /* ── ADC1: scan + continuous + DMA + SW trigger ───────────────── */
    ADC1->CR1  = ADC_CR1_SCAN;
    ADC1->CR2  = ADC_CR2_CONT
               | ADC_CR2_DMA
               | (7U << 17U)       /* EXTSEL = SWSTART               */
               | ADC_CR2_EXTTRIG;

    ADC1->SMPR2 = 0x00000B6DU;     /* SMP[2:0]=5 = 55.5 cycles CH0..CH3  */
                                    /* 0xB6D = 0b101_101_101_101       */
    ADC1->SQR1  = (3U << 20U);     /* sequence length = 4            */
    ADC1->SQR3  = (0U <<  0U) | (1U <<  5U) | (2U << 10U) | (3U << 15U);

    /* ── Power on → calibrate → start ────────────────────────────── */
    ADC1->CR2 |= ADC_CR2_ADON;
    k_busy_wait(10U);

    ADC1->CR2 |= ADC_CR2_RSTCAL;
    for (uint32_t t = 0U; t < 100000U && (ADC1->CR2 & ADC_CR2_RSTCAL); t++) {}

    ADC1->CR2 |= ADC_CR2_CAL;
    for (uint32_t t = 0U; t < 100000U && (ADC1->CR2 & ADC_CR2_CAL); t++) {}

    ADC1->CR2 |= ADC_CR2_SWSTART;
}

void adc_sensor_read(uint16_t out[ADC_NUM_CHANNELS])
{
    for (uint8_t i = 0U; i < ADC_NUM_CHANNELS; i++) {
        out[i] = s_adc[i];
    }
}

void adc_sensor_fault_reset(void)
{
    (void)memset(s_bad_count, 0, sizeof(s_bad_count));
    (void)memset(s_good_count, 0, sizeof(s_good_count));
    (void)memset(&s_fault, 0, sizeof(s_fault));
}

void adc_sensor_fault_get(adc_sensor_fault_status_t *status)
{
    if (status != NULL) {
        *status = s_fault;
    }
}

bool adc_sensor_fault_update(const uint16_t sample[ADC_NUM_CHANNELS],
                             adc_sensor_fault_status_t *status)
{
    uint8_t old_active = s_fault.active_mask;
    uint8_t old_low = s_fault.low_mask;
    uint8_t old_high = s_fault.high_mask;

    if (sample == NULL) {
        adc_sensor_fault_get(status);
        return false;
    }

    for (uint8_t i = 0U; i < ADC_NUM_CHANNELS; i++) {
        uint8_t bit = (uint8_t)(1U << i);
        bool low = (sample[i] <= ADC_SENSOR_FAULT_LOW_THRESHOLD);
        bool high = (sample[i] >= ADC_SENSOR_FAULT_HIGH_THRESHOLD);
        bool bad = low || high;
        bool active = ((s_fault.active_mask & bit) != 0U);

        if (bad) {
            s_good_count[i] = 0U;
            if (s_bad_count[i] < ADC_SENSOR_FAULT_ASSERT_COUNT) {
                s_bad_count[i]++;
            }
            if (s_bad_count[i] >= ADC_SENSOR_FAULT_ASSERT_COUNT) {
                s_fault.active_mask |= bit;
                if (low) {
                    s_fault.low_mask |= bit;
                    s_fault.high_mask &= (uint8_t)~bit;
                } else {
                    s_fault.high_mask |= bit;
                    s_fault.low_mask &= (uint8_t)~bit;
                }
            }
            continue;
        }

        s_bad_count[i] = 0U;
        if (active) {
            if (s_good_count[i] < ADC_SENSOR_FAULT_RECOVER_COUNT) {
                s_good_count[i]++;
            }
            if (s_good_count[i] >= ADC_SENSOR_FAULT_RECOVER_COUNT) {
                s_fault.active_mask &= (uint8_t)~bit;
                s_fault.low_mask &= (uint8_t)~bit;
                s_fault.high_mask &= (uint8_t)~bit;
                s_good_count[i] = 0U;
            }
        } else {
            s_good_count[i] = 0U;
        }
    }

    adc_sensor_fault_get(status);
    return (old_active != s_fault.active_mask)
        || (old_low != s_fault.low_mask)
        || (old_high != s_fault.high_mask);
}
