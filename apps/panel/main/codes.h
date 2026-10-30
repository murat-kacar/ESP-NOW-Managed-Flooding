#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Open the factory NVS partition "fctry" (namespace "factory", keys admin_code/user_code, ADR 0003).
 *
 * Codes changed with keypad commands are stored in the normal NVS partition and take precedence.
 */
esp_err_t codes_init(void);

bool codes_is_user(const char *code);
bool codes_is_admin(const char *code);

/** @brief Store a new user code (CONFIG_DB_USER_CODE_LENGTH digits). */
esp_err_t codes_set_user(const char *code);

/** @brief Store a new admin code (CONFIG_DB_ADMIN_CODE_LENGTH digits). */
esp_err_t codes_set_admin(const char *code);

/** @brief Erase the changed codes; the factory codes on the label apply again (SPEC 6.1). */
esp_err_t codes_reset(void);

/** @brief True if text is exactly length decimal digits. */
bool codes_is_valid(const char *text, int length);
