#pragma once

#include "esp_err.h"

/**
 * @brief Configure the bell relay GPIO (CONFIG_DB_BELL_RELAY_GPIO) in its inactive state.
 *
 * The output level is set before the pin becomes an output, so no pulse occurs at boot (ADR 0005 pattern).
 */
esp_err_t bell_init(void);

/**
 * @brief Pulse the bell relay for CONFIG_DB_BELL_PULSE_MS; with CONFIG_DB_VISUAL_BELL the status LED blinks too.
 *
 * A call while a pulse is active is ignored; the pulse is not extended.
 */
esp_err_t bell_ring(void);
