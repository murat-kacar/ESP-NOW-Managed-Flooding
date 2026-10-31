#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "identity.h"

/*
 * Periodic health report to the panel (ADR 0015). Counters count since the last restart.
 * With CONFIG_DB_HEARTBEAT off, nothing is scheduled and heartbeat_send() is never called.
 */

/** @brief Create the report timer. due() runs in the esp_timer task whenever a report is due. */
esp_err_t heartbeat_init(void (*due)(void));

/** @brief Schedule reports: the first after a random delay, then every interval with +/-10 % jitter. */
void heartbeat_start(void);

/** @brief Build and send one report to the paired panel; call from a task that may block. */
esp_err_t heartbeat_send(const identity_t *identity);

/** @brief Count a RING received (not a retry). */
void heartbeat_count_ring(void);

/** @brief Count an OPEN_DOOR request and whether it was acknowledged. */
void heartbeat_count_door_request(bool acknowledged);
