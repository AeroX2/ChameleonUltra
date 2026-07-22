#pragma once

#include "ble_main.h"
#include "nrfx_pwm.h"

/* Exposed so lf_gap.c can stop the PWM and drive LF_ANT_DRIVER directly
 * to create clean field gaps without relying on PWM pin release state. */
extern nrfx_pwm_t m_pwm;

#define LF_RADIO_FREQUENCY_MIN_KHZ 115
#define LF_RADIO_FREQUENCY_MAX_KHZ 135
#define LF_RADIO_FREQUENCY_DEFAULT_KHZ 125

void lf_125khz_radio_init(void);
void lf_125khz_radio_uninit(void);

void lf_125khz_radio_saadc_enable(lf_adc_callback_t cb);
void lf_125khz_radio_gpiote_enable(void);
void lf_125khz_radio_saadc_disable(void);
void lf_125khz_radio_gpiote_disable(void);

void start_lf_125khz_radio(void);
void stop_lf_125khz_radio(void);
bool lf_125khz_radio_set_frequency_khz(uint8_t frequency_khz);
uint8_t lf_125khz_radio_get_frequency_khz(void);
uint32_t lf_125khz_radio_get_actual_frequency_hz(void);
