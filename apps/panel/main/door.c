#include "door.h"

#include <stdatomic.h>

#include "db_status_led.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "door";

#if CONFIG_DB_DOOR_RELAY_ACTIVE_LOW
#define RELAY_ON_LEVEL 0
#else
#define RELAY_ON_LEVEL 1
#endif
#define RELAY_OFF_LEVEL (!RELAY_ON_LEVEL)

#define BUZZER_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER      LEDC_TIMER_1
#define BUZZER_CHANNEL    LEDC_CHANNEL_1
#define BUZZER_RESOLUTION LEDC_TIMER_10_BIT
#define BUZZER_DUTY       (((1U << 10) - 1U) * CONFIG_DB_BUZZER_DUTY_PCT / 100U)

static esp_timer_handle_t s_relay_timer;
static esp_timer_handle_t s_buzzer_timer;
static atomic_bool s_open;

static esp_err_t buzzer_set(uint32_t duty)
{
    ESP_RETURN_ON_ERROR(ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, duty), TAG, "buzzer duty failed");
    return ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

static void relay_end(void *arg)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_level(CONFIG_DB_DOOR_RELAY_GPIO, RELAY_OFF_LEVEL));
    db_status_led_stop(DB_LED_DOOR_OPEN);
    atomic_store(&s_open, false);
}

static void buzzer_end(void *arg)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(buzzer_set(0));
}

esp_err_t door_init(void)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_DB_DOOR_RELAY_GPIO, RELAY_OFF_LEVEL), TAG, "set level failed");
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_DB_DOOR_RELAY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "relay gpio config failed");

    const ledc_timer_config_t timer = {
        .speed_mode = BUZZER_MODE,
        .duty_resolution = BUZZER_RESOLUTION,
        .timer_num = BUZZER_TIMER,
        .freq_hz = CONFIG_DB_BUZZER_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "buzzer timer config failed");
    const ledc_channel_config_t channel = {
        .gpio_num = CONFIG_DB_BUZZER_GPIO,
        .speed_mode = BUZZER_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "buzzer channel config failed");

    const esp_timer_create_args_t relay_args = { .callback = relay_end, .name = "door_relay" };
    const esp_timer_create_args_t buzzer_args = { .callback = buzzer_end, .name = "door_buzzer" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&relay_args, &s_relay_timer), TAG, "timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_create(&buzzer_args, &s_buzzer_timer), TAG, "timer create failed");
    ESP_LOGI(TAG, "relay GPIO %d active %s, %d ms; buzzer GPIO %d, %d Hz, %d ms", CONFIG_DB_DOOR_RELAY_GPIO,
             RELAY_ON_LEVEL ? "HIGH" : "LOW", CONFIG_DB_DOOR_PULSE_MS, CONFIG_DB_BUZZER_GPIO,
             CONFIG_DB_BUZZER_FREQUENCY_HZ, CONFIG_DB_BUZZER_DURATION_MS);
    return ESP_OK;
}

esp_err_t door_open(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_open, &expected, true)) {
        return ESP_OK;  /* pulse active: ignored */
    }
    esp_err_t err = gpio_set_level(CONFIG_DB_DOOR_RELAY_GPIO, RELAY_ON_LEVEL);
    if (err != ESP_OK) {
        atomic_store(&s_open, false);
        ESP_LOGE(TAG, "relay on failed: %s", esp_err_to_name(err));
        return err;
    }
    db_status_led_start(DB_LED_DOOR_OPEN);  /* before the timer, so relay_end() always runs after it */
    err = esp_timer_start_once(s_relay_timer, (uint64_t)CONFIG_DB_DOOR_PULSE_MS * 1000U);
    if (err != ESP_OK) {
        relay_end(NULL);  /* no timer would end the pulse: release the relay now */
        ESP_LOGE(TAG, "relay timer failed: %s", esp_err_to_name(err));
        return err;
    }
    if (CONFIG_DB_BUZZER_DURATION_MS > 0 && ESP_ERROR_CHECK_WITHOUT_ABORT(buzzer_set(BUZZER_DUTY)) == ESP_OK &&
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_once(s_buzzer_timer,
                                          (uint64_t)CONFIG_DB_BUZZER_DURATION_MS * 1000U)) != ESP_OK) {
        buzzer_end(NULL);  /* no timer would end the tone: stop it now */
    }
    ESP_LOGI(TAG, "door open");
    return ESP_OK;
}
