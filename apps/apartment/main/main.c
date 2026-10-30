#include "esp_err.h"
#include "esp_log.h"

#include "bell.h"
#include "buttons.h"
#include "db_net.h"
#include "db_status_led.h"

static const char *TAG = "apartment";

/* Network actions are added with ESP-NOW messaging (OPEN_DOOR) and pairing (espnow_sec);
 * until then the button actions only log and show the local LED feedback. */

static void on_door_open(void)
{
    ESP_LOGI(TAG, "door-open request (ESP-NOW messaging is not implemented yet)");
}

static void on_pairing(void)
{
    ESP_LOGI(TAG, "pairing mode for %d s (pairing protocol is not implemented yet)", CONFIG_DB_PAIRING_WINDOW_S);
    db_status_led_start_for(DB_LED_PAIRING_MODE, CONFIG_DB_PAIRING_WINDOW_S * 1000U);
}

static void on_reset(void)
{
    ESP_LOGI(TAG, "reset request (identity storage is not implemented yet)");
}

void app_main(void)
{
    ESP_ERROR_CHECK(db_net_init());
    ESP_ERROR_CHECK(db_status_led_init(CONFIG_DB_STATUS_LED_GPIO));
    /* No identity is stored yet, so the module always starts unpaired (SPEC 11.2). */
    ESP_ERROR_CHECK(db_status_led_start(DB_LED_UNPAIRED));
    ESP_ERROR_CHECK(bell_init());

    const buttons_handlers_t handlers = {
        .door_open = on_door_open,
        .pairing = on_pairing,
        .reset = on_reset,
    };
    ESP_ERROR_CHECK(buttons_init(&handlers));
    ESP_LOGI(TAG, "started");
}
