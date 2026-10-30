#include "buttons.h"

#include <stdint.h>

#include "button_gpio.h"
#include "db_status_led.h"
#include "esp_check.h"
#include "esp_log.h"
#include "iot_button.h"

static const char *TAG = "buttons";

#if CONFIG_DB_BUTTONS_ACTIVE_LOW
#define ACTIVE_LEVEL 0
#else
#define ACTIVE_LEVEL 1
#endif
#if CONFIG_DB_BUTTONS_PULLUP
#define DISABLE_PULL false
#else
#define DISABLE_PULL true
#endif

#define DOOR_MIN_MS      CONFIG_DB_DOOR_BUTTON_MIN_MS
#define DOOR_MAX_MS      (CONFIG_DB_DOOR_BUTTON_MAX_S * 1000U)
#define WINDOW_OPEN_MS   (CONFIG_DB_SERVICE_HOLD_MIN_S * 1000U)
#define WINDOW_CLOSE_MS  (CONFIG_DB_SERVICE_HOLD_MAX_S * 1000U)
#define CANCEL_MS        (CONFIG_DB_SERVICE_HOLD_CANCEL_S * 1000U)

_Static_assert(DOOR_MIN_MS < DOOR_MAX_MS, "DB_DOOR_BUTTON_MIN_MS must be shorter than DB_DOOR_BUTTON_MAX_S");
_Static_assert(WINDOW_OPEN_MS < WINDOW_CLOSE_MS, "DB_SERVICE_HOLD_MIN_S must be less than DB_SERVICE_HOLD_MAX_S");
_Static_assert(WINDOW_CLOSE_MS < CANCEL_MS, "DB_SERVICE_HOLD_MAX_S must be less than DB_SERVICE_HOLD_CANCEL_S");
_Static_assert(CANCEL_MS <= UINT16_MAX, "espressif/button long-press thresholds are 16-bit milliseconds");

typedef struct {
    const char *name;
    int gpio;
    db_led_pattern_t window_pattern;
    void (*action)(void);
    button_handle_t handle;
} service_button_t;

static buttons_handlers_t s_handlers;
static button_handle_t s_door;
static service_button_t s_pairing = { .name = "pairing", .gpio = CONFIG_DB_PAIRING_BUTTON_GPIO,
                                      .window_pattern = DB_LED_PAIRING_HOLD };
static service_button_t s_reset = { .name = "reset", .gpio = CONFIG_DB_RESET_BUTTON_GPIO,
                                    .window_pattern = DB_LED_RESET_HOLD };

static esp_err_t create_button(int gpio, button_handle_t *handle)
{
    const button_config_t config = { 0 };  /* 0: espressif/button Kconfig defaults (IoT Button menu) */
    const button_gpio_config_t gpio_config = {
        .gpio_num = gpio,
        .active_level = ACTIVE_LEVEL,
        .disable_pull = DISABLE_PULL,
    };
    return iot_button_new_gpio_device(&config, &gpio_config, handle);
}

static void door_released(void *handle, void *usr_data)
{
    uint32_t pressed_ms = iot_button_get_pressed_time((button_handle_t)handle);
    if (pressed_ms < DOOR_MIN_MS || pressed_ms > DOOR_MAX_MS) {
        ESP_LOGI(TAG, "door button: %u ms press ignored", (unsigned)pressed_ms);
        return;
    }
    s_handlers.door_open();
}

static void service_window_open(void *handle, void *usr_data)
{
    db_status_led_start(((service_button_t *)usr_data)->window_pattern);
}

static void service_window_close(void *handle, void *usr_data)
{
    db_status_led_stop(((service_button_t *)usr_data)->window_pattern);
}

static void service_cancel(void *handle, void *usr_data)
{
    db_status_led_start(DB_LED_BUTTON_CANCELLED);
}

static void service_released(void *handle, void *usr_data)
{
    service_button_t *button = usr_data;
    db_status_led_stop(button->window_pattern);
    db_status_led_stop(DB_LED_BUTTON_CANCELLED);
    uint32_t pressed_ms = iot_button_get_pressed_time((button_handle_t)handle);
    if (pressed_ms < WINDOW_OPEN_MS || pressed_ms > WINDOW_CLOSE_MS) {
        ESP_LOGI(TAG, "%s button: released after %u ms, outside the release window", button->name, (unsigned)pressed_ms);
        return;
    }
    button->action();
}

static esp_err_t register_long_press(service_button_t *button, uint16_t press_ms, button_cb_t cb)
{
    button_event_args_t args = { .long_press.press_time = press_ms };
    return iot_button_register_cb(button->handle, BUTTON_LONG_PRESS_START, &args, cb, button);
}

static esp_err_t init_service_button(service_button_t *button, void (*action)(void))
{
    button->action = action;
    ESP_RETURN_ON_ERROR(create_button(button->gpio, &button->handle), TAG, "%s button create failed", button->name);
    ESP_RETURN_ON_ERROR(register_long_press(button, WINDOW_OPEN_MS, service_window_open), TAG, "register failed");
    ESP_RETURN_ON_ERROR(register_long_press(button, WINDOW_CLOSE_MS, service_window_close), TAG, "register failed");
    ESP_RETURN_ON_ERROR(register_long_press(button, CANCEL_MS, service_cancel), TAG, "register failed");
    return iot_button_register_cb(button->handle, BUTTON_PRESS_UP, NULL, service_released, button);
}

esp_err_t buttons_init(const buttons_handlers_t *handlers)
{
    ESP_RETURN_ON_FALSE(handlers && handlers->door_open && handlers->pairing && handlers->reset,
                        ESP_ERR_INVALID_ARG, TAG, "all handlers are required");
    s_handlers = *handlers;

    ESP_RETURN_ON_ERROR(create_button(CONFIG_DB_DOOR_BUTTON_GPIO, &s_door), TAG, "door button create failed");
    ESP_RETURN_ON_ERROR(iot_button_register_cb(s_door, BUTTON_PRESS_UP, NULL, door_released, NULL), TAG,
                        "register failed");
    ESP_RETURN_ON_ERROR(init_service_button(&s_pairing, handlers->pairing), TAG, "pairing button failed");
    ESP_RETURN_ON_ERROR(init_service_button(&s_reset, handlers->reset), TAG, "reset button failed");

    ESP_LOGI(TAG, "door GPIO %d, pairing GPIO %d, reset GPIO %d, active %s", CONFIG_DB_DOOR_BUTTON_GPIO,
             CONFIG_DB_PAIRING_BUTTON_GPIO, CONFIG_DB_RESET_BUTTON_GPIO, ACTIVE_LEVEL ? "HIGH" : "LOW");
    return ESP_OK;
}
