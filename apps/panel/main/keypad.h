#pragma once

#include "esp_err.h"

/** Called with the key character from CONFIG_DB_KEYPAD_KEYMAP; runs in the button timer task. */
typedef void (*keypad_handler_t)(char key);

/** @brief Create the 3x4 matrix keypad (espressif/button matrix driver) on the Kconfig GPIOs. */
esp_err_t keypad_init(keypad_handler_t handler);
