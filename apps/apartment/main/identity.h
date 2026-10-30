#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Pairing result stored in NVS (SPEC 5.1). The application key is stored by esp-now itself. */
typedef struct {
    uint8_t apartment;
    uint8_t channel;
    uint8_t panel_mac[6];
} identity_t;

/** @brief Load the stored identity; false if the module is not paired. */
bool identity_load(identity_t *out);

esp_err_t identity_save(const identity_t *identity);

esp_err_t identity_erase(void);
