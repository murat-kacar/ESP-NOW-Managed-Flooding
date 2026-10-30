#include "bell.h"

#include <stdatomic.h>

#include "db_status_led.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "bell";

#if CONFIG_DB_BELL_RELAY_ACTIVE_HIGH
#define BELL_ON_LEVEL  1
#else
#define BELL_ON_LEVEL  0
#endif
#define BELL_OFF_LEVEL (!BELL_ON_LEVEL)

static esp_timer_handle_t s_pulse_timer;
static atomic_bool s_ringing;

static void pulse_end(void *arg)
{
    gpio_set_level(CONFIG_DB_BELL_RELAY_GPIO, BELL_OFF_LEVEL);
#if CONFIG_DB_VISUAL_BELL
    db_status_led_stop(DB_LED_RINGING);
#endif
    atomic_store(&s_ringing, false);
}

esp_err_t bell_init(void)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_DB_BELL_RELAY_GPIO, BELL_OFF_LEVEL), TAG, "set level failed");
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_DB_BELL_RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "gpio config failed");

    const esp_timer_create_args_t args = { .callback = pulse_end, .name = "bell_pulse" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&args, &s_pulse_timer), TAG, "timer create failed");
    ESP_LOGI(TAG, "bell relay on GPIO %d, active %s, pulse %d ms", CONFIG_DB_BELL_RELAY_GPIO,
             BELL_ON_LEVEL ? "HIGH" : "LOW", CONFIG_DB_BELL_PULSE_MS);
    return ESP_OK;
}

esp_err_t bell_ring(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_ringing, &expected, true)) {
        return ESP_OK;  /* already ringing */
    }
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_DB_BELL_RELAY_GPIO, BELL_ON_LEVEL), TAG, "set level failed");
#if CONFIG_DB_VISUAL_BELL
    db_status_led_start(DB_LED_RINGING);
#endif
    return esp_timer_start_once(s_pulse_timer, (uint64_t)CONFIG_DB_BELL_PULSE_MS * 1000U);
}
