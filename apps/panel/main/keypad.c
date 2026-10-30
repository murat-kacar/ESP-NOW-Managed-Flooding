#include "keypad.h"

#include <stdint.h>

#include "button_matrix.h"
#include "esp_check.h"
#include "iot_button.h"

static const char *TAG = "keypad";

#define ROWS 4
#define COLS 3
#define KEYS (ROWS * COLS)

/* The matrix driver numbers keys row-major (index = row * COLS + col), the order of the key map. */
static const char s_keymap[] = CONFIG_DB_KEYPAD_KEYMAP;
_Static_assert(sizeof(CONFIG_DB_KEYPAD_KEYMAP) == KEYS + 1, "DB_KEYPAD_KEYMAP must have exactly 12 characters");

static int32_t s_rows[ROWS] = {
    CONFIG_DB_KEYPAD_ROW0_GPIO, CONFIG_DB_KEYPAD_ROW1_GPIO, CONFIG_DB_KEYPAD_ROW2_GPIO, CONFIG_DB_KEYPAD_ROW3_GPIO,
};
static int32_t s_cols[COLS] = { CONFIG_DB_KEYPAD_COL0_GPIO, CONFIG_DB_KEYPAD_COL1_GPIO, CONFIG_DB_KEYPAD_COL2_GPIO };
static button_handle_t s_keys[KEYS];
static keypad_handler_t s_handler;

static void key_pressed(void *handle, void *usr_data)
{
    s_handler(s_keymap[(intptr_t)usr_data]);
}

esp_err_t keypad_init(keypad_handler_t handler)
{
    ESP_RETURN_ON_FALSE(handler != NULL, ESP_ERR_INVALID_ARG, TAG, "handler is required");
    s_handler = handler;
    const button_config_t config = { 0 };  /* 0: espressif/button Kconfig defaults (IoT Button menu) */
    const button_matrix_config_t matrix = {
        .row_gpios = s_rows,
        .col_gpios = s_cols,
        .row_gpio_num = ROWS,
        .col_gpio_num = COLS,
    };
    size_t size = KEYS;
    ESP_RETURN_ON_ERROR(iot_button_new_matrix_device(&config, &matrix, s_keys, &size), TAG, "matrix create failed");
    for (intptr_t i = 0; i < KEYS; i++) {
        ESP_RETURN_ON_ERROR(iot_button_register_cb(s_keys[i], BUTTON_PRESS_DOWN, NULL, key_pressed, (void *)i),
                            TAG, "register failed");
    }
    return ESP_OK;
}
