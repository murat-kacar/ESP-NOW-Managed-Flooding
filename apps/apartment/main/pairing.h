#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Register the espnow_sec event handler and create the pairing timers. */
esp_err_t pairing_init(void);

/**
 * @brief Open the pairing window (ADR 0007): start the espnow_sec responder and listen on the candidate
 * channels in turn (CONFIG_DB_CHANNEL_DWELL_MS each) for CONFIG_DB_PAIRING_WINDOW_S.
 */
esp_err_t pairing_start(void);

/** @brief True if the handshake with this panel succeeded in the open window (PAIR_CONFIG may follow). */
bool pairing_is_expected_panel(const uint8_t mac[6]);

/**
 * @brief Close the window. Without success, a key received in the handshake is erased so the module stays
 * unpaired and can pair again.
 */
void pairing_finish(bool success);
