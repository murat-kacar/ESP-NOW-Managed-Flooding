#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "db_proto.h"
#include "esp_err.h"

/** A paired apartment module. */
typedef struct {
    uint8_t mac[DB_MAC_LEN];
    uint32_t session;  /**< pairing session; messages with another session are dropped */
} registry_entry_t;

/** @brief Load the apartment registry (apartment number -> module) from NVS into RAM. */
esp_err_t registry_init(void);

bool registry_get(uint8_t apartment, registry_entry_t *entry);

/** @brief Find the apartment registered for a module MAC. */
bool registry_find(const uint8_t mac[DB_MAC_LEN], uint8_t *apartment, registry_entry_t *entry);

/** @brief Register a module; fails with ESP_ERR_INVALID_STATE if the number is occupied (SPEC 5.2). */
esp_err_t registry_add(uint8_t apartment, const registry_entry_t *entry);

esp_err_t registry_remove(uint8_t apartment);

esp_err_t registry_erase_all(void);
