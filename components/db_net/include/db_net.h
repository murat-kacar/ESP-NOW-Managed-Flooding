#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise NVS, Wi-Fi (STA mode, not connected, power save off) and ESP-NOW.
 *
 * Call once at startup, before any other networking call.
 */
esp_err_t db_net_init(void);

#ifdef __cplusplus
}
#endif
