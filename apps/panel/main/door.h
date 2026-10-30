#pragma once

#include "esp_err.h"

/**
 * @brief Configure the door relay (inactive level before output mode, ADR 0005) and the buzzer (LEDC timer 1,
 * channel 1; LEDC timer 0 / channel 0 belong to the status LED).
 */
esp_err_t door_init(void);

/**
 * @brief Drive the door relay for CONFIG_DB_DOOR_PULSE_MS and the buzzer for CONFIG_DB_BUZZER_DURATION_MS.
 *
 * A request while the pulse is active is ignored; the pulse is not extended (ADR 0005).
 */
esp_err_t door_open(void);
