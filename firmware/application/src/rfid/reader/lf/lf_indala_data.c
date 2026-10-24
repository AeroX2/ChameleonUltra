#include "indala_decode.h"
#include "lf_125khz_radio.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "bsp_wdt.h"

// The firmware heap is only 8 KiB. Keep the capture in BSS rather
// than making a read depend on a heap allocation that cannot succeed.
_Static_assert(INDALA_CAPTURE_SAMPLES <= 0x7fff, "Capture must fit SAADC MAXCNT");
static int16_t capture[INDALA_CAPTURE_SAMPLES];
static volatile size_t captured;

static void indala_adc_callback(nrf_saadc_value_t *values, size_t count) {
    // DMA writes directly into capture; publish completion only after END.
    if (values == capture && count == INDALA_CAPTURE_SAMPLES) {
        __DMB();
        captured = count;
    }
}

bool indala_read(uint8_t *raw, uint32_t timeout_ms, uint8_t psk) {
    uint8_t saved_frequency = lf_125khz_radio_get_frequency_khz();
    lf_125khz_radio_set_frequency_khz(125);
    start_lf_125khz_radio();
    bsp_delay_ms(40);
    autotimer *timer = bsp_obtain_timer(0);
    bool ok = false;
    while (!ok && NO_TIMEOUT_1MS(timer, timeout_ms)) {
        captured = 0;
        lf_125khz_radio_psk_adc_enable(indala_adc_callback, capture, INDALA_CAPTURE_SAMPLES);
        while (captured < INDALA_CAPTURE_SAMPLES && NO_TIMEOUT_1MS(timer, timeout_ms)) {
            bsp_wdt_feed();
        }
        lf_125khz_radio_psk_adc_disable();
        if (captured == INDALA_CAPTURE_SAMPLES) {
            ok = indala_decode_ex(capture, captured, raw, psk);
        }
        bsp_wdt_feed();
    }
    bsp_return_timer(timer);
    stop_lf_125khz_radio();
    lf_125khz_radio_set_frequency_khz(saved_frequency);
    return ok;
}
