#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#include "utils.h"

#define SETTINGS_CURRENT_VERSION 13
#define SETTINGS_HF_RX_GAIN_DEFAULT 0x60  // 43 dB; must match RC522_RX_GAIN in rc522.c
#define SETTINGS_LF_FREQUENCY_DEFAULT_KHZ 125
#define SETTINGS_SLEEP_TIMEOUT_DEFAULT_S 8   // default wake timeout in seconds (matches SLEEP_DELAY_MS_BUTTON_WAKEUP)
#define SETTINGS_SLEEP_TIMEOUT_MIN_S      5
#define SETTINGS_SLEEP_TIMEOUT_MAX_S      60
#define BLE_PAIRING_KEY_LEN 6
#define DEFAULT_BLE_PAIRING_KEY "123456"  // length must == 6

typedef enum {
    SettingsAnimationModeFull = 0U,
    SettingsAnimationModeMinimal = 1U,
    SettingsAnimationModeSymmetric = 3U,
    SettingsAnimationModeNone = 2U,
    SettingsAnimationModeMAX = 4U,
} settings_animation_mode_t;

typedef enum {
    // Set this button to have no function
    //    (But always can wakeup device, why didn't to disable this function? i dont known, you can ask chatgpt.)
    SettingsButtonDisable = 0U,
    // Card slot number sequence will increase after pressing
    SettingsButtonCycleSlot = 1U,
    // Card slot number sequence decreases after pressing
    SettingsButtonCycleSlotDec = 2U,
    // Read the UID card number immediately after pressing, continue searching, and simulate immediately after reading the card
    SettingsButtonCloneIcUid = 3U,
    SettingsButtonShowBattery = 4U,
    // Toggle NFC field generator on/off (Ultra only, must be in reader mode)
    SettingsButtonNfcFieldGenerator = 5U,
    // Toggle the BLE radio (advertising) on/off; the choice is persisted to flash
    SettingsButtonToggleBle = 6U,
    // Read a 125kHz EM410x card and emulate it in a slot (LF UID clone, on-device)
    SettingsButtonCloneLfUid = 7U,
} settings_button_function_t;

typedef struct ALIGN_U32 {
    uint16_t version;

    // 1 byte
    uint8_t animation_config : 2;
    uint8_t ble_pairing_enable : 1;
    uint8_t ble_radio_enable : 1; // add on version9: master BLE advertising on/off
    // add on version13: low four bits of (LF carrier kHz - 115)
    uint8_t lf_frequency_low : 4;

    // 1 byte
    uint8_t button_a_press : 4;
    uint8_t button_b_press : 4;

    // 1 byte
    uint8_t button_a_long_press : 4;
    uint8_t button_b_long_press : 4;

    // 6 byte
    uint8_t ble_connect_key[6];

    // 1 byte (add on version6)
    uint8_t sleep_timeout; // wake timeout in seconds after button wakeup

    // 1 byte (add on version7)
    uint8_t button_a_double : 4;
    uint8_t button_b_double : 4;

    // 1 byte (add on version8)
    uint8_t button_chord : 4;
    // add on version12: persisted RC522 receiver gain, stored as the 3-bit
    // RFCfgReg RxGain index (4=33dB, 5=38dB, 6=43dB, 7=48dB).
    uint8_t hf_rx_gain : 3;
    // add on version13: high bit of (LF carrier kHz - 115)
    uint8_t lf_frequency_high : 1;

    /*
     * Warning !!!!!!!!!!!!!!!!!!!!!! <-------------
     * If you need to add settings,
     * please be sure to consult the documentation of the bit field
     * and fully use the space of this structure before considering reallocating memory space.
     */
} settings_data_t;

void settings_init_config(void);
void settings_migrate(void);
void settings_load_config(void);
uint8_t settings_save_config(void);
uint8_t settings_get_animation_config(void);
void settings_set_animation_config(uint8_t value);
uint8_t settings_get_button_press_config(char which);
uint8_t settings_get_long_button_press_config(char which);
uint8_t settings_get_double_button_press_config(char which);
uint8_t settings_get_chord_button_press_config(void);
void settings_set_button_press_config(char which, uint8_t value);
void settings_set_long_button_press_config(char which, uint8_t value);
void settings_set_double_button_press_config(char which, uint8_t value);
void settings_set_chord_button_press_config(uint8_t value);
void settings_init_double_button_press_config(void);
void settings_init_chord_button_press_config(void);
bool is_settings_button_type_valid(char type);
uint8_t *settings_get_ble_connect_key(void);
void settings_set_ble_connect_key(uint8_t *key);
void settings_set_ble_pairing_enable(bool enable);
bool settings_get_ble_pairing_enable(void);
bool settings_get_ble_pairing_enable_first_load(void);
void settings_set_ble_radio_enable(bool enable);
bool settings_get_ble_radio_enable(void);
void settings_init_ble_radio_enable_config(void);
uint32_t settings_get_sleep_timeout(void);
void settings_set_sleep_timeout(uint8_t seconds);
void settings_init_sleep_timeout_config(void);
uint8_t settings_get_hf_rx_gain(void);          // returns RFCfgReg byte (0x40/0x50/0x60/0x70)
void settings_set_hf_rx_gain(uint8_t reg_value);
void settings_init_hf_rx_gain_config(void);
uint8_t settings_get_lf_frequency_khz(void);
void settings_set_lf_frequency_khz(uint8_t frequency_khz);
void settings_init_lf_frequency_config(void);
#endif
