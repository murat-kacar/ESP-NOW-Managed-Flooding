#pragma once

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Pair the module next to the panel as an apartment (SPEC 5.1, ADR 0007). Blocks for up to
 * CONFIG_DB_PAIRING_WINDOW_S plus the handshake.
 *
 * @return ESP_OK                    paired and registered
 *         ESP_ERR_INVALID_STATE     apartment number already registered
 *         ESP_ERR_NOT_FOUND         no unpaired module answered in the window
 *         ESP_ERR_INVALID_RESPONSE  more than one unpaired module answered: pairing cancelled
 *         other                     handshake or configuration message failed
 */
esp_err_t pairing_run(uint8_t apartment);
