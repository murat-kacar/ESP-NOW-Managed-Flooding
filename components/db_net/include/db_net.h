#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DB_CHANNEL_CANDIDATE_COUNT 3

/**
 * espnow_sec Proof of Possession. espnow_sec requires a non-NULL PoP (ESP_PARAM_CHECK in the component);
 * ADR 0007 uses the same non-secret value on all devices.
 */
#define DB_SEC_POP "doorbell"

/**
 * @brief Initialise NVS, Wi-Fi (STA, not connected, power save off), radio settings and ESP-NOW with security.
 *
 * Radio settings come from the "Doorbell network" Kconfig menu: TX power, optional country code, Long Range.
 * A stored application key (from pairing) is loaded, so traffic is encrypted from boot.
 */
esp_err_t db_net_init(void);

/** @brief Set the Wi-Fi channel used for ESP-NOW. */
esp_err_t db_net_set_channel(uint8_t channel);

/** @brief Current primary Wi-Fi channel. */
uint8_t db_net_get_channel(void);

/** @brief Candidate channels from Kconfig (DB_CHANNEL_CANDIDATE_1..3). */
void db_net_channel_candidates(uint8_t out[DB_CHANNEL_CANDIDATE_COUNT]);

/**
 * @brief Site scan (ADR 0006): choose the candidate with the fewest APs; on a tie, the lowest maximum RSSI.
 */
esp_err_t db_net_scan_best_channel(uint8_t *channel);

/** @brief True when an application key is stored (the device is paired or is a panel with a key). */
bool db_net_has_key(void);

/** @brief Panel: create and store a random application key if none exists (espnow security example). */
esp_err_t db_net_ensure_key(void);

/** @brief Erase the stored application key (module reset, building reset). */
esp_err_t db_net_erase_key(void);

#ifdef __cplusplus
}
#endif
