#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** @brief Load the apartment registry (apartment number -> module MAC) from NVS into RAM. */
esp_err_t registry_init(void);

bool registry_get(uint8_t apartment, uint8_t mac[6]);

/** @brief Find the apartment registered for a module MAC. */
bool registry_find(const uint8_t mac[6], uint8_t *apartment);

/** @brief Register a module; fails with ESP_ERR_INVALID_STATE if the number is occupied (SPEC 5.2). */
esp_err_t registry_add(uint8_t apartment, const uint8_t mac[6]);

esp_err_t registry_remove(uint8_t apartment);

esp_err_t registry_erase_all(void);
