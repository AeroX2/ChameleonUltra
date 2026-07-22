#include <stdio.h>

#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "lf_reader_main.h"
#include "nrfx_saadc.h"
#include "protocols/hidprox.h"
#include "time.h"
#include "rfid_main.h"

#define NRF_LOG_MODULE_NAME lf_read
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define HIDPROX_BUFFER_SIZE (6144)

static circular_buffer cb;

// saadc irq is used to sample ANT GPIO.
static void saadc_cb(nrf_saadc_value_t *vals, size_t size) {
    for (int i = 0; i < size; i++) {
        nrf_saadc_value_t val = vals[i];
        if (!cb_push_back(&cb, &val)) {
            return;
        }
    }
}

static void init_hidprox_hw(void) {
    lf_125khz_radio_saadc_enable(saadc_cb);
}

static void uninit_hidprox_hw(void) {
    lf_125khz_radio_saadc_disable();
}

static bool hidprox_capture(hidprox_codec *codec, uint32_t timeout_ms) {
    cb_init(&cb, HIDPROX_BUFFER_SIZE, sizeof(uint16_t));
    init_hidprox_hw();
    start_lf_125khz_radio();

    bool ok = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t val = 0;
        while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms) && cb_pop_front(&cb, &val)) {
            if (hidprox.decoder.feed(codec, val)) {
                ok = true;
                break;
            }
        }
    }

    bsp_return_timer(p_at);
    stop_lf_125khz_radio();
    uninit_hidprox_hw();
    cb_free(&cb);

    return ok;
}

bool hidprox_read(uint8_t *data, uint8_t format_hint, uint32_t timeout_ms) {
    hidprox_codec *codec = hidprox.alloc();
    hidprox.decoder.start(codec, format_hint);

    bool ok = hidprox_capture(codec, timeout_ms);
    if (ok) {
        memcpy(data, hidprox.get_data(codec), hidprox.data_size);
    }

    hidprox.free(codec);
    return ok;
}

bool hidprox_read_candidates(wiegand_candidate_t *candidates, size_t capacity, size_t *count,
                             uint8_t format_hint, uint32_t timeout_ms) {
    hidprox_codec *codec = hidprox.alloc();
    hidprox.decoder.start(codec, format_hint);
    codec->return_all_candidates = true;

    bool ok = hidprox_capture(codec, timeout_ms);
    if (ok) {
        *count = hidprox_get_candidates(codec, candidates, capacity);
    } else {
        *count = 0;
    }

    hidprox.free(codec);
    return ok;
}
