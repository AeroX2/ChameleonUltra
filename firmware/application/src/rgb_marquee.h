#ifndef RGB_MARQUEE_H
#define RGB_MARQUEE_H

#include <stdint.h>
#include "nrf_drv_pwm.h"


void rgb_marquee_init(void);
void rgb_marquee_stop(void);
void rgb_marquee_reset(void);
bool rgb_marquee_is_enabled(void);
void rgb_marquee_usb_open_sweep(uint8_t color, uint8_t dir);
void rgb_marquee_usb_open_symmetric(uint8_t color);
void rgb_marquee_sweep_to(uint8_t color, uint8_t dir, uint8_t end);
void rgb_marquee_slot_switch(uint8_t led_down, uint8_t color_led_down, uint8_t led_up, uint8_t color_led_up);
void rgb_marquee_sweep_fade(uint8_t color, uint8_t dir, uint8_t end, uint8_t start_light, uint8_t stop_light);
void rgb_marquee_sweep_from_to(uint8_t color, uint8_t start, uint8_t stop);
void rgb_marquee_usb_idle(void);
void rgb_marquee_symmetric_out(uint8_t color, uint8_t slot);
void rgb_marquee_symmetric_in(uint8_t color, uint8_t slot);

// Non-blocking boot/wake-up animation engine.
// Build a sequence with rgb_marquee_boot_clear()/rgb_marquee_boot_push(), start
// it with rgb_marquee_boot_run(), then call rgb_marquee_boot_process() from the
// main loop until rgb_marquee_boot_is_active() returns false. On completion the
// final slot colour is applied and on_complete (e.g. light_up_by_slot) is called.
#define RGB_BOOT_SWEEP_TO       0
#define RGB_BOOT_SWEEP_FROM_TO  1
#define RGB_BOOT_SYMMETRIC_OUT  2
#define RGB_BOOT_SYMMETRIC_IN   3

void rgb_marquee_boot_clear(void);
void rgb_marquee_boot_push(uint8_t type, uint8_t color, uint8_t p1, uint8_t p2);
void rgb_marquee_boot_run(uint8_t final_color, void (*on_complete)(void));
bool rgb_marquee_boot_is_active(void);
void rgb_marquee_boot_process(void);

#endif
