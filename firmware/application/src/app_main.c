#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "nordic_common.h"
#include "nrf.h"

#include "app_timer.h"
#include "app_usbd.h"
#include "app_util_platform.h"
#include "nrf_delay.h"
#include "nrf_drv_gpiote.h"
#include "nrf_drv_rng.h"
#include "nfc_mf1.h"         // for nfc_tag_mf1_prng_seed
#include "nrf_power.h"
#include "nrf_pwr_mgmt.h"
#include "nrfx_nfct.h"
#include "nrfx_power.h"
#include "nrf_drv_lpcomp.h"
#include "nrf_ble_lesc.h"

#define NRF_LOG_MODULE_NAME app_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "app_cmd.h"
#include "ble_main.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "bsp_wdt.h"
#include "dataframe.h"
#include "fds_ids.h"
#include "fds_util.h"
#include "hex_utils.h"
#include "rfid_main.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "usb_main.h"
#include "rgb_marquee.h"
#include "tag_persistence.h"
#include "settings.h"

#if defined(PROJECT_CHAMELEON_ULTRA)
#include "rc522.h"
#endif

// Defining soft timers
APP_TIMER_DEF(m_button_check_timer); // Timer for button debounce
APP_TIMER_DEF(m_button_a_long_press_timer); // Per-button long-hold detection (A)
APP_TIMER_DEF(m_button_b_long_press_timer); // Per-button long-hold detection (B)
APP_TIMER_DEF(m_button_a_dblclick_timer);   // Awaiting second-click window (A)
APP_TIMER_DEF(m_button_b_dblclick_timer);   // Awaiting second-click window (B)

#define BUTTON_LONG_HOLD_MS 1000
// Window to wait for a second click before dispatching a single-click. Kept
// short so single presses (e.g. slot switching) stay snappy; trades a little
// double-click timing tolerance for lower single-click latency.
#define BUTTON_DBLCLICK_WINDOW_MS 200
// Max gap between the two presses for an A+B chord to register. Widened from
// 80ms so the chord is reliably triggerable by hand (tradeoff: a fast A-then-B
// can occasionally count as a chord rather than two separate presses).
#define BUTTON_CHORD_WINDOW_MS 200
// Boot-time slot select: after a button wake, how long to keep counting extra
// A taps before committing. Each tap restarts the window. Tune on-device.
#define BOOT_SLOT_CLICK_WINDOW_MS 250

static bool m_is_b_btn_press = false;
static bool m_is_a_btn_press = false;

static bool m_is_b_btn_release = false;
static bool m_is_a_btn_release = false;

// Press timestamps for chord detection: the second press must be debounced
// within BUTTON_CHORD_WINDOW_MS of the first to count as a chord.
static uint32_t m_a_press_time = 0;
static uint32_t m_b_press_time = 0;

// Chord state: when both buttons are pressed together, individual events
// for both are suppressed and a single chord event is dispatched once.
static bool m_chord_active = false;
static bool m_chord_pending = false;

// Long-hold fires immediately when threshold is reached while button is still held.
// Once fired for a button, the subsequent release does NOT also emit a short-click.
static bool m_a_long_hold_fired = false;
static bool m_b_long_hold_fired = false;
static bool m_a_long_hold_pending = false;
static bool m_b_long_hold_pending = false;

// Double-click detection: after the first release, we hold off the short-click
// dispatch for BUTTON_DBLCLICK_WINDOW_MS to see whether a second press arrives.
// Only engaged when double-click is configured for that button.
static bool m_a_await_2nd = false;
static bool m_b_await_2nd = false;
static bool m_a_in_2nd_press = false;
static bool m_b_in_2nd_press = false;
static bool m_a_dblclick_pending = false;
static bool m_b_dblclick_pending = false;

static bool m_system_off_processing = false;

// NFC field generator state
volatile bool m_is_field_on = false;

// cpu reset reason
static uint32_t m_reset_source;
static uint32_t m_gpregret_val;

#define GPREGRET_CLEAR_VALUE_DEFAULT (0xFFFFFFFFUL)
#define RESET_ON_LF_FIELD_EXISTS_Msk (1UL)

extern bool g_is_low_battery_shutdown;


/**@brief Function for assert macro callback.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyse
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in] line_num    Line number of the failing ASSERT call.
 * @param[in] p_file_name File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) {
    // /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

/**@brief Function for initializing the timer module.
 */
static void app_timers_init(void) {
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the nrf log module.
 */
static void log_init(void) {
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief Function for initializing power management.
 */
static void power_management_init(void) {
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing power management.
 */
void rng_drv_and_srand_init(void) {
    ret_code_t err_code;
    uint8_t available;
    uint32_t rand_int;

    // First initialize the official rng management driver api
    err_code = nrf_drv_rng_init(NULL);
    APP_ERROR_CHECK(err_code);

    // Wait for the random number generator to generate enough random numbers to put in the queue
    do {
        nrf_drv_rng_bytes_available(&available);
    } while (available < 4);

    // Note that here we are forcing the address of a uint32_t value to be converted to a uint8_t address
    // to get the pointer to the first byte of uint32
    err_code = nrf_drv_rng_rand(((uint8_t *)(&rand_int)), 4);
    APP_ERROR_CHECK(err_code);

    // Finally initialize the srand seeds in the c standard library
    srand(rand_int);

    // Seed the MFC LFSR PRNG with the same hardware random value.
    // This makes nonce generation follow the real Mifare Classic LFSR pattern
    // so readers that fingerprint PRNG type (e.g. Eltis) accept the emulated card.
    nfc_tag_mf1_prng_seed(rand_int);
}

/**@brief Initialize GPIO matrix library
 */
static void gpio_te_init(void) {
    // Initialize GPIOTE
    uint32_t err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);
}

#if defined(PROJECT_CHAMELEON_ULTRA)
static void field_generator_rainbow_loop(void) {
    static uint8_t color_index = 0;
    static uint32_t last_update = 0;

    if (!m_is_field_on) return;

    uint32_t now = app_timer_cnt_get();

    if (app_timer_cnt_diff_compute(now, last_update) < APP_TIMER_TICKS(100)) {
        return;
    }
    last_update = now;

    // Rainbow colors
    const uint8_t colors[] = {RGB_RED, RGB_YELLOW, RGB_GREEN, RGB_CYAN, RGB_BLUE, RGB_MAGENTA};

    set_slot_light_color(colors[color_index]);
    uint32_t *led_pins = hw_get_led_array();

    // Light up all LEDs with current color
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_set(led_pins[i]);
    }

    color_index = (color_index + 1) % 6;
}
#endif

/**@brief Button Matrix Events
 */
static void button_pin_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
    device_mode_t mode = get_device_mode();
    // Allow button operations in both tag and reader mode
    if (mode == DEVICE_MODE_TAG || mode == DEVICE_MODE_READER) {
        static nrf_drv_gpiote_pin_t pin_static;                                  // Use static internal variables to store the GPIO where the current event occurred
        pin_static = pin;                                                        // Cache the button that currently triggers the event into an internal variable
        app_timer_start(m_button_check_timer, APP_TIMER_TICKS(25), &pin_static); // Start timer anti-shake (25ms debounce for snappier button response)
    }
}

/** @brief Long-hold timer callback for button A: fires when A has been held
 *         continuously for BUTTON_LONG_HOLD_MS without release.
 */
static void timer_button_a_long_hold_handle(void *arg) {
    (void)arg;
    if (m_is_a_btn_press) {
        NRF_LOG_INFO("BUTTON_A_LONG_HOLD");
        m_a_long_hold_fired = true;
        m_a_long_hold_pending = true;
    }
}

/** @brief Long-hold timer callback for button B. */
static void timer_button_b_long_hold_handle(void *arg) {
    (void)arg;
    if (m_is_b_btn_press) {
        NRF_LOG_INFO("BUTTON_B_LONG_HOLD");
        m_b_long_hold_fired = true;
        m_b_long_hold_pending = true;
    }
}

/** @brief Double-click window expired for button A: no second press arrived
 *         within BUTTON_DBLCLICK_WINDOW_MS, so dispatch the first click as
 *         a single short-press.
 */
static void timer_button_a_dblclick_handle(void *arg) {
    (void)arg;
    if (m_a_await_2nd) {
        m_a_await_2nd = false;
        m_is_a_btn_release = true;
    }
}

/** @brief Double-click window expired for button B. */
static void timer_button_b_dblclick_handle(void *arg) {
    (void)arg;
    if (m_b_await_2nd) {
        m_b_await_2nd = false;
        m_is_b_btn_release = true;
    }
}

/** @brief Cancel all pending single-button event state when transitioning into
 *         chord mode. Suppresses any in-flight long-hold/double-click/release
 *         that would otherwise fire alongside the chord.
 */
static void enter_chord_state(void) {
    app_timer_stop(m_button_a_long_press_timer);
    app_timer_stop(m_button_b_long_press_timer);
    app_timer_stop(m_button_a_dblclick_timer);
    app_timer_stop(m_button_b_dblclick_timer);
    m_a_long_hold_fired = false;
    m_b_long_hold_fired = false;
    m_a_long_hold_pending = false;
    m_b_long_hold_pending = false;
    m_a_await_2nd = false;
    m_b_await_2nd = false;
    m_a_in_2nd_press = false;
    m_b_in_2nd_press = false;
    m_a_dblclick_pending = false;
    m_b_dblclick_pending = false;
    m_is_a_btn_release = false;
    m_is_b_btn_release = false;
    m_chord_active = true;
    m_chord_pending = true;
    NRF_LOG_INFO("BUTTON_CHORD");
}

/** @brief Button anti-shake timer
 * @param None
 * @return None
 */
static void timer_button_event_handle(void *arg) {
    // if button press during shutdown, it's only to wake up quickly
    if (m_system_off_processing) {
        m_system_off_processing = false;
        NRF_LOG_INFO("BUTTON press during shutdown");
        return;
    }

    nrf_drv_gpiote_pin_t pin = *(nrf_drv_gpiote_pin_t *)arg;

    // Check here if the current GPIO is at the pressed level
    if (nrf_gpio_pin_read(pin) == 1) {
        if (pin == BUTTON_1) {
            // If button is disabled, we can't dispatch key event.
            if (settings_get_button_press_config('b') != SettingsButtonDisable) {
                uint32_t now = app_timer_cnt_get();
                // Chord detection: if A was pressed within CHORD_WINDOW_MS, treat as chord.
                if (m_is_a_btn_press && !m_chord_active &&
                    app_timer_cnt_diff_compute(now, m_a_press_time) < APP_TIMER_TICKS(BUTTON_CHORD_WINDOW_MS)) {
                    enter_chord_state();
                    m_is_b_btn_press = true;
                    m_b_press_time = now;
                } else if (m_b_await_2nd) {
                    // Second press of a potential double-click — cancel the await
                    // window timer and mark this press as the second one. We do
                    // not arm long-hold on the second press; double-click takes
                    // precedence.
                    app_timer_stop(m_button_b_dblclick_timer);
                    m_b_await_2nd = false;
                    m_b_in_2nd_press = true;
                    m_is_b_btn_press = true;
                    m_b_press_time = now;
                    NRF_LOG_INFO("BUTTON_B_PRESS_2ND");
                } else {
                    NRF_LOG_INFO("BUTTON_B_PRESS");
                    m_is_b_btn_press = true;
                    m_b_press_time = now;
                    m_b_long_hold_fired = false;
                    app_timer_start(m_button_b_long_press_timer,
                                    APP_TIMER_TICKS(BUTTON_LONG_HOLD_MS), NULL);
                }
            }
        }
        if (pin == BUTTON_2) {
            if (settings_get_button_press_config('a') != SettingsButtonDisable) {
                uint32_t now = app_timer_cnt_get();
                if (m_is_b_btn_press && !m_chord_active &&
                    app_timer_cnt_diff_compute(now, m_b_press_time) < APP_TIMER_TICKS(BUTTON_CHORD_WINDOW_MS)) {
                    enter_chord_state();
                    m_is_a_btn_press = true;
                    m_a_press_time = now;
                } else if (m_a_await_2nd) {
                    app_timer_stop(m_button_a_dblclick_timer);
                    m_a_await_2nd = false;
                    m_a_in_2nd_press = true;
                    m_is_a_btn_press = true;
                    m_a_press_time = now;
                    NRF_LOG_INFO("BUTTON_A_PRESS_2ND");
                } else {
                    NRF_LOG_INFO("BUTTON_A_PRESS");
                    m_is_a_btn_press = true;
                    m_a_press_time = now;
                    m_a_long_hold_fired = false;
                    app_timer_start(m_button_a_long_press_timer,
                                    APP_TIMER_TICKS(BUTTON_LONG_HOLD_MS), NULL);
                }
            }
        }
    }

    if (nrf_gpio_pin_read(pin) == 0) {
        if (pin == BUTTON_1 && m_is_b_btn_press == true) {
            app_timer_stop(m_button_b_long_press_timer);
            if (m_chord_active) {
                // While chord is active, release of either button just clears
                // that button's state. The chord event already fired on press.
                m_is_b_btn_press = false;
                NRF_LOG_INFO("BUTTON_B_RELEASE_CHORD");
                if (!m_is_a_btn_press) m_chord_active = false;
                return;
            }
            // If button is disabled, we can't dispatch key event.
            if (settings_get_button_press_config('b') != SettingsButtonDisable) {
                m_is_b_btn_press = false;
                if (m_b_long_hold_fired) {
                    // Long-hold action already fired during the hold — suppress release event.
                    NRF_LOG_INFO("BUTTON_B_RELEASE_AFTER_LONG");
                    m_b_long_hold_fired = false;
                } else if (m_b_in_2nd_press) {
                    // Release of the second click — dispatch as double-click.
                    NRF_LOG_INFO("BUTTON_B_RELEASE_DBL");
                    m_b_in_2nd_press = false;
                    m_b_dblclick_pending = true;
                } else if (settings_get_double_button_press_config('b') != SettingsButtonDisable) {
                    // First release with double-click configured — hold off the
                    // short-press dispatch in case a second click arrives.
                    NRF_LOG_INFO("BUTTON_B_RELEASE_AWAIT_2ND");
                    m_b_await_2nd = true;
                    app_timer_start(m_button_b_dblclick_timer,
                                    APP_TIMER_TICKS(BUTTON_DBLCLICK_WINDOW_MS), NULL);
                } else {
                    NRF_LOG_INFO("BUTTON_B_RELEASE_SHORT");
                    m_is_b_btn_release = true;
                }
            }
        }
        if (pin == BUTTON_2 && m_is_a_btn_press == true) {
            app_timer_stop(m_button_a_long_press_timer);
            if (m_chord_active) {
                m_is_a_btn_press = false;
                NRF_LOG_INFO("BUTTON_A_RELEASE_CHORD");
                if (!m_is_b_btn_press) m_chord_active = false;
                return;
            }
            if (settings_get_button_press_config('a') != SettingsButtonDisable) {
                m_is_a_btn_press = false;
                if (m_a_long_hold_fired) {
                    NRF_LOG_INFO("BUTTON_A_RELEASE_AFTER_LONG");
                    m_a_long_hold_fired = false;
                } else if (m_a_in_2nd_press) {
                    NRF_LOG_INFO("BUTTON_A_RELEASE_DBL");
                    m_a_in_2nd_press = false;
                    m_a_dblclick_pending = true;
                } else if (settings_get_double_button_press_config('a') != SettingsButtonDisable) {
                    NRF_LOG_INFO("BUTTON_A_RELEASE_AWAIT_2ND");
                    m_a_await_2nd = true;
                    app_timer_start(m_button_a_dblclick_timer,
                                    APP_TIMER_TICKS(BUTTON_DBLCLICK_WINDOW_MS), NULL);
                } else {
                    NRF_LOG_INFO("BUTTON_A_RELEASE_SHORT");
                    m_is_a_btn_release = true;
                }
            }
        }
    }
}

/**@brief Function for init button and led.
 */
static void button_init(void) {
    ret_code_t err_code;

    // Non-exact timer for initializing button anti-shake
    err_code = app_timer_create(&m_button_check_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_event_handle);
    APP_ERROR_CHECK(err_code);

    // Per-button long-hold timers (fire after BUTTON_LONG_HOLD_MS of continuous press).
    err_code = app_timer_create(&m_button_a_long_press_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_a_long_hold_handle);
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_create(&m_button_b_long_press_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_b_long_hold_handle);
    APP_ERROR_CHECK(err_code);

    // Per-button double-click window timers.
    err_code = app_timer_create(&m_button_a_dblclick_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_a_dblclick_handle);
    APP_ERROR_CHECK(err_code);
    err_code = app_timer_create(&m_button_b_dblclick_timer, APP_TIMER_MODE_SINGLE_SHOT, timer_button_b_dblclick_handle);
    APP_ERROR_CHECK(err_code);

    // Configure SENSE mode, select false for sense configuration
    nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(false);
    in_config.pull = NRF_GPIO_PIN_PULLDOWN; // Pulldown

    // Configure key binding POTR
    err_code = nrf_drv_gpiote_in_init(BUTTON_1, &in_config, button_pin_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_gpiote_in_event_enable(BUTTON_1, true);

    err_code = nrf_drv_gpiote_in_init(BUTTON_2, &in_config, button_pin_handler);
    APP_ERROR_CHECK(err_code);
    nrf_drv_gpiote_in_event_enable(BUTTON_2, true);
}

/**@brief The implementation function to enter deep hibernation
 */
static void system_off_enter(void) {
    ret_code_t ret;
    m_system_off_processing = true;
    // Flash save (FDS) and the sd_power_* hibernation calls below require the
    // SoftDevice to be enabled. If the BLE stack was skipped at boot (radio
    // disabled), bring it up now before saving so those paths behave normally.
    if (!is_ble_initialized()) {
        NRF_LOG_INFO("Late BLE init for flash save before sleep");
        ble_slave_init();
    }
    // Save tag data
    tag_emulation_save();

    if (g_is_low_battery_shutdown) {
        // Don't create too complex animations, just blink LED1 three times.
        rgb_marquee_stop();
        set_slot_light_color(RGB_RED);
        for (uint8_t i = 0; i <= 3; i++) {
            nrf_gpio_pin_set(LED_1);
            bsp_delay_ms(100);
            nrf_gpio_pin_clear(LED_1);
            bsp_delay_ms(100);
        }
    } else {
        // close all led.
        uint32_t *p_led_array = hw_get_led_array();
        for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_clear(p_led_array[i]);
        }
        // Power off animation
        uint8_t animation_config = settings_get_animation_config();
        uint8_t slot = tag_emulation_get_slot();
        uint8_t dir = slot > 3 ? 1 : 0;
        uint8_t color = get_color_by_slot(slot);
        if (m_reset_source & (NRF_POWER_RESETREAS_NFC_MASK | NRF_POWER_RESETREAS_LPCOMP_MASK)) {
            if (m_reset_source & NRF_POWER_RESETREAS_NFC_MASK) {
                color = 1;
            } else {
                color = 2;
            }
        }
        if (animation_config == SettingsAnimationModeFull) {
            if (m_system_off_processing) rgb_marquee_sweep_from_to(color, slot, dir ? 7 : 0);
            if (m_system_off_processing) rgb_marquee_sweep_fade(color, dir, 7, 99, 75);
            if (m_system_off_processing) rgb_marquee_sweep_fade(color, !dir, 7, 75, 50);
            if (m_system_off_processing) rgb_marquee_sweep_fade(color, dir, 7, 50, 25);
            if (m_system_off_processing) rgb_marquee_sweep_fade(color, !dir, 7, 25, 0);
        } else if (animation_config == SettingsAnimationModeMinimal) {
            if (m_system_off_processing) rgb_marquee_sweep_from_to(color, slot, !dir ? 7 : 0);
        } else if (animation_config == SettingsAnimationModeSymmetric) {
            if (m_system_off_processing) rgb_marquee_symmetric_in(color, slot);
        }
        rgb_marquee_stop();
        if (!m_system_off_processing) {
            for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
                nrf_gpio_pin_clear(p_led_array[i]);
            }
            light_up_by_slot();
            sleep_timer_start(SLEEP_DELAY_MS_BUTTON_CLICK);
            return;
        }
    }

    // Disable the HF NFC event first
    NRF_NFCT->INTENCLR = NRF_NFCT_DISABLE_ALL_INT;
    // Then disable the LF LPCOMP event
    NRF_LPCOMP->INTENCLR = LPCOMP_INTENCLR_CROSS_Msk | LPCOMP_INTENCLR_UP_Msk | LPCOMP_INTENCLR_DOWN_Msk | LPCOMP_INTENCLR_READY_Msk;

    // Configure RAM hibernation hold
    uint32_t ram8_retention = // RAM8 Each section has 32KB capacity
        // POWER_RAM_POWER_S0RETENTION_On << POWER_RAM_POWER_S0RETENTION_Pos ;
        // POWER_RAM_POWER_S1RETENTION_On << POWER_RAM_POWER_S1RETENTION_Pos |
        // POWER_RAM_POWER_S2RETENTION_On << POWER_RAM_POWER_S2RETENTION_Pos |
        // POWER_RAM_POWER_S3RETENTION_On << POWER_RAM_POWER_S3RETENTION_Pos |
        // POWER_RAM_POWER_S4RETENTION_On << POWER_RAM_POWER_S4RETENTION_Pos |
        POWER_RAM_POWER_S5RETENTION_On << POWER_RAM_POWER_S5RETENTION_Pos;
    ret = sd_power_ram_power_set(8, ram8_retention);
    APP_ERROR_CHECK(ret);

    // IOs that need to be configured as floating analog inputs ==> no pull-up or pull-down
    uint32_t gpio_cfg_default_no_pull[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_SPI_SELECT,
        HF_SPI_MISO,
        HF_SPI_MOSI,
        HF_SPI_MOSI,
        LF_OA_OUT,
#endif
        BAT_SENSE_PIN,
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_default_no_pull); i++) {
        nrf_gpio_cfg_default(gpio_cfg_default_no_pull[i]);
    }

    // IO that needs to be configured as a push-pull output and pulled high
    uint32_t gpio_cfg_output_high[] = {
#if defined(PROJECT_CHAMELEON_ULTRA)
        HF_ANT_SEL,
#endif
        LED_FIELD, LED_R, LED_G, LED_B,
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_output_high); i++) {
        nrf_gpio_cfg_output(gpio_cfg_output_high[i]);
        nrf_gpio_pin_set(gpio_cfg_output_high[i]);
    }

    // IOs that need to be configured as push-pull outputs and pulled low
    uint32_t gpio_cfg_output_low[] = {
        LED_1, LED_2, LED_3, LED_4, LED_5, LED_6, LED_7, LED_8, LF_MOD,
#if defined(PROJECT_CHAMELEON_ULTRA)
        READER_POWER, LF_ANT_DRIVER
#endif
    };
    for (int i = 0; i < ARRAY_SIZE(gpio_cfg_output_low); i++) {
        nrf_gpio_cfg_output(gpio_cfg_output_low[i]);
        nrf_gpio_pin_clear(gpio_cfg_output_low[i]);
    }

    // Wait for a while before hibernating to avoid GPIO circuit configuration fluctuations to wake up the chip
    bsp_delay_ms(50);

    // Print leaving message finally
    NRF_LOG_INFO("Sleep finally, Bye ^.^");
    // Turn off all soft timers
    app_timer_stop_all();

    // Check whether there are low -frequency fields, solving very strong field signals during dormancy have always caused the comparator to be at a high level input state, so that the problem of uprising the rising edge cannot be awakened.
    if (is_lf_field_exists()) {
        // Close the comparator
        nrf_drv_lpcomp_disable();
        // Set the reason for Reset. After restarting, you need to get this reason to avoid misjudgment from the source of wake up.
        sd_power_gpregret_clr(1, GPREGRET_CLEAR_VALUE_DEFAULT);
        sd_power_gpregret_set(1, RESET_ON_LF_FIELD_EXISTS_Msk);
        // Trigger the RESET awakening system, restart the emulation process
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_RESET);
        return;
    };

    // Last call, gate is closing
    NRF_LOG_FLUSH();

    // Go to system-off mode (this function will not return; wakeup will cause a reset).
    // Note that if you insert jlink or drive a Debug, you may report an error when entering the low power consumption.
    // When starting debugging, we should disable low power consumption state values, or simply not enter low power consumption
    ret = sd_power_system_off();

    // OK, here is very important. If you open the log output and enable RTT, you will not check the error of the low power mode
#if !(NRF_LOG_ENABLED && NRF_LOG_BACKEND_RTT_ENABLED)
    APP_ERROR_CHECK(ret);
#else
    UNUSED_VARIABLE(ret);
#endif

    // It is not supposed to enter here, but jlink debug mode it can be entered, at most is not normal hibernation just
    // jlink connection, power consumption will rise, and hibernation will also be stuck in this step.
    while (1)
        NRF_LOG_PROCESS();
}

/**
 *@brief :Detection of wake-up source
 */
static void check_wakeup_src(void) {
    // The reset reason and GPREGRET2 must be read via the SoftDevice when it is
    // enabled, but through direct register access when the BLE stack was skipped
    // at boot (radio disabled in settings).
    if (is_ble_initialized()) {
        sd_power_reset_reason_get(&m_reset_source);
        sd_power_reset_reason_clr(m_reset_source);

        sd_power_gpregret_get(1, &m_gpregret_val);
        sd_power_gpregret_clr(1, GPREGRET_CLEAR_VALUE_DEFAULT);
    } else {
        m_reset_source = nrf_power_resetreas_get();
        nrf_power_resetreas_clear(m_reset_source);

        m_gpregret_val = NRF_POWER->GPREGRET2;
        NRF_POWER->GPREGRET2 = 0;
    }


    /*
     * Note: The hibernation described below is deep hibernation, stopping any non-wakeup source peripherals and stopping the CPU to achieve the lowest power consumption
     *
     * If the wake-up source is a button, then you need to turn on BLE broadcast until the button stops clicking after a period of time to hibernate
     * If the wake-up source is the field of the analog card, it is not necessary to turn on BLE broadcast until the analog card ends and then hibernate.
     * If the wake-up source is USB, then keep BLE on and no hibernation until USB is unplugged
     * If the wakeup source is the first time to access the battery, then do nothing and go directly to hibernation
     *
     * Hint: The above; logic is the logic processed in the wake-up phase, the rest of the logic is converted to the runtime processing phase
     */

    uint8_t slot = tag_emulation_get_slot();
    uint8_t dir = slot > 3 ? 1 : 0;
    uint8_t color = get_color_by_slot(slot);

    if (m_reset_source & NRF_POWER_RESETREAS_OFF_MASK) {
        NRF_LOG_INFO("WakeUp from button");

        // Boot-time slot select: the device woke from an A press (the B button
        // is claimed by the bootloader for DFU entry, so it never reaches the
        // app). The wake press counts as the first tap; count up to 3 quick A
        // taps and select slot 0/1/2 for 1/2/3 taps. Done before the wake
        // animation below so the animation targets the chosen slot (no stale
        // LED on the previously-selected slot).
        uint8_t boot_taps = 1;
        uint32_t tap_gap_ms = 0;
        bool a_was_down = (nrf_gpio_pin_read(BUTTON_2) == 1);
        while (boot_taps < 3 && tap_gap_ms < BOOT_SLOT_CLICK_WINDOW_MS) {
            bool a_is_down = (nrf_gpio_pin_read(BUTTON_2) == 1);
            if (a_is_down && !a_was_down) {  // rising edge = another tap
                boot_taps++;
                tap_gap_ms = 0;
            } else {
                bsp_delay_ms(5);
                tap_gap_ms += 5;
            }
            a_was_down = a_is_down;
        }
        uint8_t boot_slot = boot_taps - 1;  // 1/2/3 taps -> slot 0/1/2
        if (boot_slot != tag_emulation_get_slot()) {
            NRF_LOG_INFO("Boot slot select: %d tap(s) -> slot %d", boot_taps, boot_slot + 1);
            tag_emulation_change_slot(boot_slot, false);
            slot = tag_emulation_get_slot();
            dir = slot > 3 ? 1 : 0;
            color = get_color_by_slot(slot);
        }

        advertising_start(false); // Turn on Bluetooth radio

        // Button wake-up boot animation (non-blocking; plays from the main loop)
        uint8_t animation_config = settings_get_animation_config();
        rgb_marquee_boot_clear();
        if (animation_config == SettingsAnimationModeFull) {
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, color, !dir, 11);
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, color, dir, 11);
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, color, !dir, dir ? slot : 7 - slot);
        } else if (animation_config == SettingsAnimationModeMinimal) {
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, color, !dir, dir ? slot : 7 - slot);
        } else if (animation_config == SettingsAnimationModeSymmetric) {
            rgb_marquee_boot_push(RGB_BOOT_SYMMETRIC_OUT, color, slot, 0);
        }

        // The indicator of the current card slot lights up at the end of the animation
        rgb_marquee_boot_run(color, light_up_by_slot);

        // If no operation follows, wait for the timeout and then deep hibernate
        sleep_timer_start(SLEEP_DELAY_MS_BUTTON_WAKEUP);
    } else if ((m_reset_source & (NRF_POWER_RESETREAS_NFC_MASK | NRF_POWER_RESETREAS_LPCOMP_MASK)) ||
               (m_gpregret_val & RESET_ON_LF_FIELD_EXISTS_Msk)) {
        NRF_LOG_INFO("WakeUp from rfid field");

        // wake up from hf field.
        if (m_reset_source & NRF_POWER_RESETREAS_NFC_MASK) {
            color = 1;  // HF field show G.
            NRF_LOG_INFO("WakeUp from HF");
        } else {
            color = 2;  // LF filed show B.
            if (m_gpregret_val & RESET_ON_LF_FIELD_EXISTS_Msk) {
                NRF_LOG_INFO("Reset by LF");
            } else {
                NRF_LOG_INFO("WakeUp from LF");
            }
        }

        // It is currently the wake-up system of the emulation card event, we can make the strong lights on the field first
        TAG_FIELD_LED_ON();

        uint8_t animation_config = settings_get_animation_config();
        rgb_marquee_boot_clear();
        if (animation_config == SettingsAnimationModeFull) {
            // In the case of field wake-up, only one round of RGB is swept as the power-on animation
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, color, !dir, dir ? slot : 7 - slot);
        } else if (animation_config == SettingsAnimationModeSymmetric) {
            rgb_marquee_boot_push(RGB_BOOT_SYMMETRIC_OUT, color, slot, 0);
        }
        rgb_marquee_boot_run(color, light_up_by_slot);

        // We can only run tag emulation at field wakeup source.
        sleep_timer_start(SLEEP_DELAY_MS_FIELD_WAKEUP);
    } else if (m_reset_source & NRF_POWER_RESETREAS_VBUS_MASK) {
        // nrfx_power_usbstatus_get() can check usb attach status
        NRF_LOG_INFO("WakeUp from VBUS(USB)");

        // USB plugged in and open communication break has its own light effect, no need to light up for the time being
        // set_slot_light_color(color);
        // light_up_by_slot();

        // Start Bluetooth radio with USB plugged in, no deep hibernation required
        advertising_start(false);
    } else {
        NRF_LOG_INFO("First power system");

        // Reset the noinit ram area
        uint32_t *noinit_addr = (uint32_t *)0x20038000;
        memset(noinit_addr, 0xFF, 0x8000);
        NRF_LOG_INFO("Reset noinit ram done.");

        // Initialize the default card slot data.
        tag_emulation_factory_init();

        // RGB (non-blocking; plays from the main loop)
        uint8_t animation_config = settings_get_animation_config();
        rgb_marquee_boot_clear();
        if (animation_config == SettingsAnimationModeFull) {
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, 0, !dir, 11);
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, 1, dir, 11);
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_TO, 2, !dir, 11);
        } else if (animation_config == SettingsAnimationModeMinimal) {
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_FROM_TO, 0, 0, 2);
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_FROM_TO, 1, 2, 5);
            rgb_marquee_boot_push(RGB_BOOT_SWEEP_FROM_TO, 2, 5, 7);
        } else if (animation_config == SettingsAnimationModeSymmetric) {
            rgb_marquee_boot_push(RGB_BOOT_SYMMETRIC_OUT, 0, ~0, 0);
            rgb_marquee_boot_push(RGB_BOOT_SYMMETRIC_IN, 1, ~0, 0);
            rgb_marquee_boot_push(RGB_BOOT_SYMMETRIC_OUT, 2, ~0, 0);
        }

        // Show RGB for slot at the end of the animation.
        rgb_marquee_boot_run(color, light_up_by_slot);

        // If the USB is plugged in when first powered up, we can do something accordingly
        if (nrfx_power_usbstatus_get() != NRFX_POWER_USB_STATE_DISCONNECTED) {
            NRF_LOG_INFO("USB Power found.");
            // usb plugged in can broadcast BLE at will
            advertising_start(false);
        } else {
            sleep_timer_start(SLEEP_DELAY_MS_FIRST_POWER); // Wait a while and go straight to hibernation, do nothing
        }
    }
}

/**@brief change slot
 */
static void cycle_slot(bool dec) {
    // In any case, a button event occurs and we need to get the currently active card slot first
    uint8_t slot_now = tag_emulation_get_slot();
    uint8_t slot_new = slot_now;
    // Handle the events of a button
    if (dec) {
        slot_new = tag_emulation_slot_find_prev(slot_now);
    } else {
        slot_new = tag_emulation_slot_find_next(slot_now);
    }
    // Update status only if the new card slot switch is valid
    tag_emulation_change_slot(slot_new, true); // Tell the analog card module that we need to switch card slots
    // Turn off the LEDs in case we were showing the battery status
    rgb_marquee_stop();
    uint32_t *led_pins = hw_get_led_array();
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(led_pins[i]);
    }
    // Go back to the color corresponding to the field enablement type
    apply_slot_change(slot_now, slot_new);
}

static void show_battery(void) {
    rgb_marquee_stop();
    uint32_t *led_pins = hw_get_led_array();
    // if still in the first 4s after boot, blink red while waiting for battery info
    while (percentage_batt_lvl == 0) {
        for (int i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_clear(led_pins[i]);
        }
        bsp_delay_ms(100);
        set_slot_light_color(RGB_RED);
        for (int i = 0; i < RGB_LIST_NUM; i++) {
            nrf_gpio_pin_set(led_pins[i]);
        }
        bsp_delay_ms(100);
    }
    // ok we have data, show level with cyan LEDs
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(led_pins[i]);
    }
    set_slot_light_color(RGB_CYAN);
    uint8_t nleds = (percentage_batt_lvl * 2) / 25; // 0->7 (8 for 100% but this is ignored)
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        if (i <= nleds) {
            nrf_gpio_pin_set(led_pins[i]);
            bsp_delay_ms(50);
        }
    }
    // nothing special to finish, we wait for sleep or slot change
}

#if defined(PROJECT_CHAMELEON_ULTRA)

static void offline_status_blink_color(uint8_t blink_color) {
    uint8_t slot = tag_emulation_get_slot();

    uint8_t color = get_color_by_slot(slot);

    uint32_t *p_led_array = hw_get_led_array();

    set_slot_light_color(blink_color);

    for (uint8_t i = 0; i < RGB_LIST_NUM; i++) {
        if (i == slot) {
            continue;
        }
        nrf_gpio_pin_set(p_led_array[i]);
        bsp_delay_ms(10);
        nrf_gpio_pin_clear(p_led_array[i]);
        bsp_delay_ms(10);
    }

    set_slot_light_color(color);
}

static void offline_status_error(void) {
    offline_status_blink_color(RGB_RED);
}

static void offline_status_ok(void) {
    offline_status_blink_color(RGB_GREEN);
}

// ----- Scratch slot persistence (used by CLONE / future FULLREAD) -----
//
// CLONE no longer overwrites the currently-active slot. Instead it lands in
// a "scratch" slot: the first empty HF slot found at the time CLONE is first
// pressed. That choice is persisted in flash so subsequent CLONE presses
// reuse the same slot (and overwrite whatever was previously cloned there).
// If no empty slot is ever available, CLONE falls back to the current active
// slot as a temporary target without persisting the choice.

#define SCRATCH_SLOT_NONE 0xFF

static uint8_t scratch_slot_get(void) {
    uint8_t buf[4] = { 0 };
    uint16_t len = sizeof(buf);
    if (fds_read_sync(FDS_SCRATCH_SLOT_FILE_ID, FDS_SCRATCH_SLOT_RECORD_KEY, &len, buf)) {
        if (buf[0] < TAG_MAX_SLOT_NUM) {
            return buf[0];
        }
    }
    return SCRATCH_SLOT_NONE;
}

static void scratch_slot_set(uint8_t idx) {
    // Word-aligned single-byte payload (FDS stores in 4-byte words).
    uint8_t buf[4] = { idx, 0, 0, 0 };
    fds_write_sync(FDS_SCRATCH_SLOT_FILE_ID, FDS_SCRATCH_SLOT_RECORD_KEY, sizeof(buf), buf);
}

// Returns the lowest slot index whose HF type is undefined (i.e. nothing
// emulated on the HF side), or SCRATCH_SLOT_NONE if all slots have HF set.
static uint8_t find_first_empty_hf_slot(void) {
    for (uint8_t i = 0; i < TAG_MAX_SLOT_NUM; i++) {
        tag_slot_specific_type_t types;
        tag_emulation_get_specific_types_by_slot(i, &types);
        if (types.tag_hf == TAG_TYPE_UNDEFINED) {
            return i;
        }
    }
    return SCRATCH_SLOT_NONE;
}

// Decide where a clone capture should land. Persists the choice on first
// use so subsequent captures reuse the same scratch slot.
//   - if a scratch slot is already persisted: use it (overwrite)
//   - else find the first empty HF slot, persist it, use it
//   - else fall back to the currently-active slot (temporary, not persisted)
static uint8_t pick_target_slot(void) {
    uint8_t scratch = scratch_slot_get();
    if (scratch != SCRATCH_SLOT_NONE) {
        return scratch;
    }
    uint8_t empty = find_first_empty_hf_slot();
    if (empty != SCRATCH_SLOT_NONE) {
        scratch_slot_set(empty);
        return empty;
    }
    return tag_emulation_get_slot();
}

// Poll the HF reader for up to timeout_ms milliseconds for a 14443A tag.
// Returns STATUS_HF_TAG_OK on success (with tag info in *tag), or the last
// scan status (typically STATUS_HF_TAG_NO) on timeout. Antenna is assumed
// to already be on. Feeds the watchdog and yields between attempts.
static uint8_t poll_for_hf_tag(picc_14a_tag_t *tag, uint32_t timeout_ms) {
    uint8_t status = STATUS_HF_TAG_NO;
    autotimer *p_at = bsp_obtain_timer(0);
    while (NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        status = pcd_14a_reader_scan_auto(tag);
        if (status == STATUS_HF_TAG_OK) {
            break;
        }
        // ~50 ms backoff between attempts; feed WDT periodically.
        for (int i = 0; i < 50; i++) {
            bsp_delay_ms(1);
            bsp_wdt_feed();
        }
    }
    bsp_return_timer(p_at);
    return status;
}

// Wait up to 5s for a 14443A tag, then write its UID/ATQA/SAK/ATS into a
// scratch slot, persist, and switch the device to that slot. Falls back to
// the active slot if no empty slot is available; fails (red LED) if no tag
// is presented within the 5s window.
static void btn_fn_copy_ic_uid(void) {
    bool was_in_reader_mode = get_device_mode() == DEVICE_MODE_READER;
    if (!was_in_reader_mode) {
        reader_mode_enter();
        bsp_delay_ms(8);
        NRF_LOG_INFO("CLONE: entered reader mode for offline UID copy");
    }

    pcd_14a_reader_antenna_on();
    bsp_delay_ms(8);

    picc_14a_tag_t tag;
    uint8_t status = poll_for_hf_tag(&tag, 5000);

    pcd_14a_reader_antenna_off();

    if (status != STATUS_HF_TAG_OK) {
        NRF_LOG_INFO("CLONE: no HF tag found within 5s");
        offline_status_error();
        if (!was_in_reader_mode) {
            tag_mode_enter();
        }
        return;
    }

    uint8_t target = pick_target_slot();
    NRF_LOG_INFO("CLONE: target slot = %d", target);

    // Switch to the target slot first so subsequent change_type / buffer
    // operations affect the right slot's in-RAM data (get_buffer_by_tag_type
    // returns the per-tag-type buffer which is shared across slots and
    // mirrors whichever slot is currently active).
    if (target != tag_emulation_get_slot()) {
        tag_emulation_change_slot(target, true);
    }

    // Make sure the target slot has HF enabled and is set to MFC 1K. If the
    // slot was empty we have to provision a type; if it already had a
    // different HF type we overwrite to MFC 1K (UID-only clones land there).
    tag_slot_specific_type_t target_types;
    tag_emulation_get_specific_types_by_slot(target, &target_types);
    if (target_types.tag_hf != TAG_TYPE_MIFARE_1024) {
        tag_emulation_change_type(target, TAG_TYPE_MIFARE_1024);
    }
    tag_emulation_slot_set_enable(target, TAG_SENSE_HF, true);

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_MIFARE_1024);
    if (buffer == NULL) {
        NRF_LOG_ERROR("CLONE: no buffer for MFC 1K");
        offline_status_error();
        if (!was_in_reader_mode) {
            tag_mode_enter();
        }
        return;
    }
    nfc_tag_mf1_information_t *p_info = (nfc_tag_mf1_information_t *)buffer->buffer;
    nfc_tag_14a_coll_res_entity_t *antres = &(p_info->res_coll);

    antres->size = tag.uid_len;
    memcpy(antres->uid, tag.uid, tag.uid_len);
    memcpy(antres->atqa, tag.atqa, 2);
    antres->sak[0] = tag.sak;
    antres->ats.length = tag.ats_len;
    memcpy(antres->ats.data, tag.ats, tag.ats_len);
    NRF_LOG_INFO("CLONE: UID copied into slot %d", target);

    // Persist nick + slot data.
    char *nick = "cloned";
    uint8_t nick_buffer[36];
    nick_buffer[0] = strlen(nick);
    memcpy(nick_buffer + 1, nick, nick_buffer[0]);
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_nick(target, TAG_SENSE_HF, &map_info);
    fds_write_sync(map_info.id, map_info.key, sizeof(nick_buffer), nick_buffer);

    tag_emulation_save();

    offline_status_ok();

    if (!was_in_reader_mode) {
        tag_mode_enter();
    }

    // CLONE may have switched to a different (scratch/empty) slot; refresh the
    // LEDs so the now-active slot is the one lit.
    light_up_by_slot();
}

#endif

/**@brief Execute the corresponding logic based on the functional settings of the buttons.
 */
// Forward declaration: defined later, but needed by the runtime BLE toggle to
// set the pairing passkey after an on-demand stack init.
static void ble_passkey_init(void);

// Toggle the BLE radio (advertising) on/off and persist the choice to flash.
// Bound to the A+B chord by default (see settings_init_chord_button_press_config).
// Brief all-LED blink to acknowledge a BLE-radio toggle. Without this the A+B
// chord toggle is silent and the user has no way to tell whether BLE just
// turned on or off. Implemented inline (not via the PROJECT_CHAMELEON_ULTRA-only
// offline_status_blink_color helper) so it also works on the lite variant.
static void ble_toggle_blink(uint8_t color) {
    uint32_t *led_pins = hw_get_led_array();
    set_slot_light_color(color);
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_set(led_pins[i]);
    }
    bsp_delay_ms(150);
    for (int i = 0; i < RGB_LIST_NUM; i++) {
        nrf_gpio_pin_clear(led_pins[i]);
    }
    // Restore the normal per-slot LED indication.
    light_up_by_slot();
}

static void btn_fn_toggle_ble(void) {
    bool enable = !settings_get_ble_radio_enable();
    settings_set_ble_radio_enable(enable);
    settings_save_config();
    if (enable) {
        NRF_LOG_INFO("BLE radio enabled");
        // The stack may have been skipped at boot; bring it up before advertising.
        if (!is_ble_initialized()) {
            ble_slave_init();
            ble_passkey_init();
        }
        advertising_start(false);
        ble_toggle_blink(RGB_BLUE); // blue = BLE on
    } else {
        NRF_LOG_INFO("BLE radio disabled");
        // Drop any live link first, then stop advertising so the radio truly
        // goes quiet (advertising alone leaves a connected host talking).
        ble_disconnect();
        advertising_stop();
        ble_toggle_blink(RGB_RED); // red = BLE off
    }
}

static void run_button_function_by_settings(settings_button_function_t sbf) {
    switch (sbf) {
        case SettingsButtonCycleSlot:
            cycle_slot(false);
            break;
        case SettingsButtonCycleSlotDec:
            cycle_slot(true);
            break;

#if defined(PROJECT_CHAMELEON_ULTRA)
        case SettingsButtonCloneIcUid:
            btn_fn_copy_ic_uid();
            break;
        case SettingsButtonNfcFieldGenerator:
            if (!m_is_field_on) {
                // Initialize reader hardware if not already in reader mode
                device_mode_t current_mode = get_device_mode();
                if (current_mode != DEVICE_MODE_READER) {
                    // Temporarily init reader hardware just for the field
                    nrf_gpio_cfg_output(READER_POWER);
                    nrf_gpio_pin_set(READER_POWER);     // reader power enable
                    nrf_gpio_cfg_output(HF_ANT_SEL);
                    nrf_gpio_pin_clear(HF_ANT_SEL);     // hf ant switch to reader mode

                    pcd_14a_reader_init();
                    bsp_delay_ms(10);
                }

                pcd_14a_reader_reset();
                pcd_14a_reader_antenna_on();
                m_is_field_on = true;
                NRF_LOG_INFO("NFC field ON");

                // Set initial rainbow state
                set_slot_light_color(RGB_RED);
                uint32_t *led_pins = hw_get_led_array();
                for (int i = 0; i < RGB_LIST_NUM; i++) {
                    nrf_gpio_pin_set(led_pins[i]);
                }

                // Stop sleep timer while field is active
                NRF_LOG_INFO("Stopping sleep timer for field generator");
                sleep_timer_stop();
                NRF_LOG_INFO("Sleep timer stopped");
            } else {
                pcd_14a_reader_antenna_off();
                m_is_field_on = false;
                NRF_LOG_INFO("NFC field OFF");

                // If we're not in reader mode, clean up the hardware
                device_mode_t current_mode = get_device_mode();
                if (current_mode != DEVICE_MODE_READER) {
                    pcd_14a_reader_uninit();
                    nrf_gpio_pin_clear(READER_POWER);   // reader power disable
                    nrf_gpio_pin_set(HF_ANT_SEL);       // hf ant switch back to tag mode
                }

                // Restore normal LED
                light_up_by_slot();

                // Restart sleep timer
                NRF_LOG_INFO("Field off, restarting sleep timer");
                sleep_timer_start(SLEEP_DELAY_MS_BUTTON_CLICK);
                NRF_LOG_INFO("Sleep timer restarted");
            }
            break;
#endif

        case SettingsButtonShowBattery:
            show_battery();
            break;

        case SettingsButtonToggleBle:
            btn_fn_toggle_ble();
            break;

        default:
            NRF_LOG_ERROR("Unsupported button function");
            break;
    }
}

/**@brief button press event process
 */
extern bool g_usb_led_marquee_enable;
static void button_press_process(void) {
    bool dispatched = false;

    // Chord event fires immediately on detection (when the second button is
    // pressed within the chord window of the first). Per-button events are
    // suppressed for both buttons until both release.
    if (m_chord_pending) {
        m_chord_pending = false;
        run_button_function_by_settings(settings_get_chord_button_press_config());
        dispatched = true;
    }
    // Long-hold events fire while the button is still pressed (mid-hold), not on release.
    if (m_a_long_hold_pending) {
        m_a_long_hold_pending = false;
        run_button_function_by_settings(settings_get_long_button_press_config('a'));
        dispatched = true;
    }
    if (m_b_long_hold_pending) {
        m_b_long_hold_pending = false;
        run_button_function_by_settings(settings_get_long_button_press_config('b'));
        dispatched = true;
    }
    // Double-click events fire on release of the second click within the window.
    if (m_a_dblclick_pending) {
        m_a_dblclick_pending = false;
        run_button_function_by_settings(settings_get_double_button_press_config('a'));
        dispatched = true;
    }
    if (m_b_dblclick_pending) {
        m_b_dblclick_pending = false;
        run_button_function_by_settings(settings_get_double_button_press_config('b'));
        dispatched = true;
    }
    // Short-click events fire on release (only when no long-hold or double-click fired).
    if (m_is_a_btn_release) {
        m_is_a_btn_release = false;
        run_button_function_by_settings(settings_get_button_press_config('a'));
        dispatched = true;
    }
    if (m_is_b_btn_release) {
        m_is_b_btn_release = false;
        run_button_function_by_settings(settings_get_button_press_config('b'));
        dispatched = true;
    }

    if (dispatched) {
        // Disable led marquee for usb at button pressed.
        g_usb_led_marquee_enable = false;
        // Re-delay into hibernation (unless field is on)
        if (!m_is_field_on) {
            sleep_timer_start(SLEEP_DELAY_MS_BUTTON_CLICK);
        }
    }
}

extern bool g_usb_port_opened;
static void blink_usb_led_status(void) {
    uint8_t slot = tag_emulation_get_slot();
    uint8_t color = get_color_by_slot(slot);
    uint8_t dir = slot > 3 ? 1 : 0;
    static bool is_working = false;
    if (nrfx_power_usbstatus_get() == NRFX_POWER_USB_STATE_DISCONNECTED) {
        if (is_working) {
            rgb_marquee_stop();
            set_slot_light_color(color);
            light_up_by_slot();
            is_working = false;
        }
    } else {
        // The light effect is enabled and can be displayed
        if (rgb_marquee_is_enabled()) {
            is_working = true;
            if (g_usb_port_opened) {
                uint8_t animation_config = settings_get_animation_config();
                if (animation_config == SettingsAnimationModeSymmetric) {
                    rgb_marquee_usb_open_symmetric(color);
                } else {
                    rgb_marquee_usb_open_sweep(color, dir);
                }
            } else {
                rgb_marquee_usb_idle();
            }
        } else {
            if (is_working) {
                is_working = false;
                rgb_marquee_stop();
                set_slot_light_color(color);
                light_up_by_slot();
            }
        }
    }
}

static void lesc_event_process(void) {
    if (is_ble_initialized() && settings_get_ble_pairing_enable_first_load()) {
        ret_code_t err_code;
        err_code = nrf_ble_lesc_request_handler();
        APP_ERROR_CHECK(err_code);
    }
}

static void ble_passkey_init(void) {
    if (is_ble_initialized() && settings_get_ble_pairing_enable_first_load()) {
        set_ble_connect_key(settings_get_ble_connect_key());
    }
}

/**@brief Application main function.
 */
int main(void) {
    hw_connect_init();        // Remember to initialize the pins first

    fds_util_init();          // Initialize fds tool
    settings_load_config();   // Load settings from flash

    init_leds();              // LED initialization
    log_init();               // Log initialization
    gpio_te_init();           // Initialize GPIO matrix library
    app_timers_init();        // Initialize soft timer
    power_management_init();  // Power management initialization
    usb_cdc_init();           // USB cdc emulation initialization
    battery_monitor_init();   // Battery sampling (runs regardless of BLE state)
    // Only bring up the SoftDevice/BLE stack when the radio is enabled. When the
    // user has switched BLE off (A+B chord), skipping this saves the ~500ms
    // SoftDevice init and its idle power draw. The stack is brought up on demand
    // later if needed (runtime toggle on, or flash save in system_off_enter).
    if (settings_get_ble_radio_enable()) {
        ble_slave_init();     // Bluetooth protocol stack initialization
    } else {
        NRF_LOG_INFO("BLE radio disabled in settings, skipping stack init");
    }

    rng_drv_and_srand_init(); // Random number generator initialization
    bsp_timer_init();         // Initialize timeout timer
    bsp_timer_start();        // Start BSP TIMER and prepare it for processing business logic
    button_init();            // Button initialization for handling business logic
    sleep_timer_init();       // Soft timer initialization for hibernation
    tag_emulation_init();     // Analog card initialization
    rgb_marquee_init();       // Light effect initialization

    ble_passkey_init();       // init ble connect key.

    // cmd callback register
    on_data_frame_complete(on_data_frame_received);

    check_wakeup_src();       // Detect wake-up source and decide BLE broadcast and subsequent hibernation action according to the wake-up source

    tag_mode_enter();         // Enter card emulation mode by default

    // usbd event listener
    APP_ERROR_CHECK(app_usbd_power_events_enable());

    bsp_wdt_init();
    // Enter main loop.
    NRF_LOG_INFO("Chameleon working");
    while (1) {
        // process lesc event
        lesc_event_process();
        // Button event process
        button_press_process();

#if defined(PROJECT_CHAMELEON_ULTRA)
        // Field generator rainbow animation
        field_generator_rainbow_loop();
#endif

        // Non-blocking boot/wake-up LED animation (advances one frame per interval)
        rgb_marquee_boot_process();

        // Led blink at usb status (only if field generator is off and boot
        // animation isn't currently driving the LEDs)
        if (!m_is_field_on && !rgb_marquee_boot_is_active()) {
            blink_usb_led_status();
        }

        // Data pack process
        data_frame_process();
        // Log print process
        while (NRF_LOG_PROCESS());
        // USB event process
        while (app_usbd_event_queue_process());
        // WDT refresh
        bsp_wdt_feed();
        // No task to process, system sleep enter.
        // If system idle sometime, we can enter deep sleep state.
        // Some task process done, we can enter cpu sleep state.
        sleep_system_run(system_off_enter, nrf_pwr_mgmt_run);
    }
}
