#pragma once

#include "esp_err.h"

/** Actions requested by the apartment module's buttons. Called from the button timer task; keep them short. */
typedef struct {
    void (*door_open)(void);  /**< door-open button released after a valid press */
    void (*pairing)(void);    /**< hidden pairing button released inside the release window */
    void (*reset)(void);      /**< hidden reset button released inside the release window */
} buttons_handlers_t;

/**
 * @brief Create the three buttons (espressif/button) from the "Doorbell apartment module" Kconfig menu.
 *
 * Door button: valid when released after CONFIG_DB_DOOR_BUTTON_MIN_MS to CONFIG_DB_DOOR_BUTTON_MAX_S.
 * Pairing and reset buttons: the LED shows the release window from CONFIG_DB_SERVICE_HOLD_MIN_S to
 * CONFIG_DB_SERVICE_HOLD_MAX_S, and the cancel pattern from CONFIG_DB_SERVICE_HOLD_CANCEL_S; only a release
 * inside the window triggers the action.
 */
esp_err_t buttons_init(const buttons_handlers_t *handlers);
