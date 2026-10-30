#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status LED patterns (SPEC 11), highest priority first.
 *
 * Several patterns may be active at once; the LED shows the highest-priority one.
 * DB_LED_OFF is always active as the lowest-priority base.
 */
typedef enum {
    DB_LED_ERROR,             /**< Fast blink; use db_status_led_start_for() with CONFIG_DB_LED_ERROR_MS */
    DB_LED_DOOR_OPEN,         /**< Solid on while the door relay is driven */
    DB_LED_SUCCESS,           /**< Full brightness for CONFIG_DB_LED_SUCCESS_MS, then ends by itself */
    DB_LED_KEY_PRESS,         /**< One short flash, then ends by itself */
    DB_LED_NO_RESPONSE,       /**< Medium blink; use db_status_led_start_for() with CONFIG_DB_LED_NO_RESPONSE_MS */
    DB_LED_BUTTON_CANCELLED,  /**< Repeating triple flash: service button held too long */
    DB_LED_RINGING,           /**< Fast blink: visual bell */
    DB_LED_PAIRING_HOLD,      /**< Medium blink: pairing button release window */
    DB_LED_RESET_HOLD,        /**< Solid with an occasional blink: reset button release window */
    DB_LED_PAIRING_MODE,      /**< Slow blink: module searching for the panel's channel */
    DB_LED_PAIRING_WINDOW,    /**< Repeating double flash: panel pairing window open */
    DB_LED_COMMAND_ENTRY,     /**< Slow blink: panel command being entered */
    DB_LED_LOCKED,            /**< Sparse single flashes: keypad locked */
    DB_LED_UNPAIRED,          /**< Repeating double flash: module not paired */
    DB_LED_PAIRED_IDLE,       /**< Dim, solid: module paired */
    DB_LED_OFF,               /**< Off: base state, always active */
    DB_LED_PATTERN_COUNT,
} db_led_pattern_t;

/**
 * @brief Initialise the status LED on a GPIO (LEDC timer 0, channel 0) and start DB_LED_OFF.
 *
 * Polarity, brightness and timings come from the "Doorbell status LED" Kconfig menu.
 */
esp_err_t db_status_led_init(int gpio_num);

/** @brief Start a pattern; it runs until stopped or until it ends by itself. */
esp_err_t db_status_led_start(db_led_pattern_t pattern);

/** @brief Stop a pattern; the LED falls back to the highest-priority pattern still active. */
esp_err_t db_status_led_stop(db_led_pattern_t pattern);

/** @brief Start a pattern and stop it after duration_ms. */
esp_err_t db_status_led_start_for(db_led_pattern_t pattern, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
