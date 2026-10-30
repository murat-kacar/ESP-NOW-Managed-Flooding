#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_proto.h"
#include "esp_err.h"

/** @brief Register the espnow_sec event handler and create the pairing timer. */
esp_err_t pairing_init(void);

/**
 * @brief Open the pairing window (ADR 0007) for CONFIG_DB_PAIRING_WINDOW_S: start the espnow_sec responder and,
 * on each candidate channel in turn, broadcast DB_OP_PAIR_HELLO and wait CONFIG_DB_CHANNEL_DWELL_MS for the
 * panel's DB_OP_PAIR_HELLO_ACK. On the answer the module stays on that channel for the handshake.
 */
esp_err_t pairing_start(void);

/** @brief Discovery frames from db_proto (protocol task). */
void pairing_on_hello(const uint8_t src[DB_MAC_LEN], db_opcode_t opcode);

/** @brief True if the handshake with this panel succeeded in the open window (PAIR_CONFIG may follow). */
bool pairing_is_expected_panel(const uint8_t mac[DB_MAC_LEN]);

/**
 * @brief Close the window. Without success, any stored key is erased: an unpaired module holds no key.
 */
void pairing_finish(bool success);
