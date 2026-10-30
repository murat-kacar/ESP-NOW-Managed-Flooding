#include <string.h>

#include "button_gpio.h"
#include "codes.h"
#include "db_net.h"
#include "db_proto.h"
#include "db_status_led.h"
#include "door.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "input.h"
#include "iot_button.h"
#include "keypad.h"
#include "nvs.h"
#include "pairing.h"
#include "registry.h"

static const char *TAG = "panel";

#define APP_QUEUE_LENGTH 16
#define APP_TASK_STACK   6144
#define APP_TASK_PRIO    4
#define NVS_NAMESPACE    "db_panel"
#define KEY_CHANNEL      "channel"

#if CONFIG_DB_CODE_RESET_BUTTON_ACTIVE_LOW
#define RESET_ACTIVE_LEVEL 0
#else
#define RESET_ACTIVE_LEVEL 1
#endif
#if CONFIG_DB_CODE_RESET_BUTTON_PULLUP
#define RESET_DISABLE_PULL false
#else
#define RESET_DISABLE_PULL true
#endif
_Static_assert(CONFIG_DB_CODE_RESET_HOLD_MIN_S < CONFIG_DB_CODE_RESET_HOLD_MAX_S,
               "DB_CODE_RESET_HOLD_MIN_S must be less than DB_CODE_RESET_HOLD_MAX_S");

typedef enum { EVENT_KEY, EVENT_INPUT_TIMEOUT, EVENT_CODE_RESET } event_type_t;

typedef struct {
    event_type_t type;
    char key;
} app_event_t;

static QueueHandle_t s_events;
static button_handle_t s_reset_button;

static void post(app_event_t event)
{
    if (xQueueSend(s_events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full");
    }
}

static void on_key(char key) { post((app_event_t) { .type = EVENT_KEY, .key = key }); }
static void on_input_timeout(void) { post((app_event_t) { .type = EVENT_INPUT_TIMEOUT }); }

static void on_reset_released(void *handle, void *usr_data)
{
    uint32_t pressed_ms = iot_button_get_pressed_time((button_handle_t)handle);
    if (pressed_ms >= CONFIG_DB_CODE_RESET_HOLD_MIN_S * 1000U && pressed_ms <= CONFIG_DB_CODE_RESET_HOLD_MAX_S * 1000U) {
        post((app_event_t) { .type = EVENT_CODE_RESET });
    } else {
        ESP_LOGI(TAG, "code reset button: %u ms press ignored", (unsigned)pressed_ms);
    }
}

static void show_result(esp_err_t err)
{
    if (err == ESP_OK) {
        db_status_led_start(DB_LED_SUCCESS);
    } else {
        db_status_led_start_for(DB_LED_ERROR, CONFIG_DB_LED_ERROR_MS);
    }
}

static void ring(uint8_t apartment)
{
    registry_entry_t entry;
    if (!registry_get(apartment, &entry)) {
        ESP_LOGI(TAG, "apartment %u is not registered", apartment);
        show_result(ESP_ERR_NOT_FOUND);
        return;
    }
    db_msg_t msg = { .opcode = DB_OP_RING, .apartment = apartment, .session = entry.session };
    esp_err_t err = db_proto_request(entry.mac, &msg, DB_OP_RING_ACK, CONFIG_DB_RING_RETRIES);
    if (err == ESP_OK) {
        db_status_led_start(DB_LED_SUCCESS);
    } else if (err == ESP_ERR_TIMEOUT) {
        db_status_led_start_for(DB_LED_NO_RESPONSE, CONFIG_DB_LED_NO_RESPONSE_MS);
    } else {
        show_result(err);
    }
    ESP_LOGI(TAG, "ring apartment %u: %s", apartment, esp_err_to_name(err));
}

static esp_err_t erase_namespace(const char *ns)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(ns, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    esp_err_t err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void building_reset(void)
{
    /* SPEC 6.3: apartment records, counters, channel and application key; codes are kept. */
    ESP_LOGW(TAG, "building reset");
    ESP_ERROR_CHECK_WITHOUT_ABORT(registry_erase_all());
    ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_erase_all_counters());
    ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_erase_key());
    ESP_ERROR_CHECK_WITHOUT_ABORT(erase_namespace(NVS_NAMESPACE));
    esp_restart();  /* boots without a channel and runs the site scan (SPEC 7.5) */
}

static void on_command(const command_t *command)
{
    switch (command->type) {
    case COMMAND_RING:
        ring(command->apartment);
        break;
    case COMMAND_OPEN_DOOR:
        door_open();
        break;
    case COMMAND_PAIR:
        show_result(pairing_run(command->apartment));
        break;
    case COMMAND_DELETE: {
        registry_entry_t entry;
        show_result(registry_get(command->apartment, &entry) ? registry_remove(command->apartment)
                                                             : ESP_ERR_NOT_FOUND);
        break;
    }
    case COMMAND_SET_USER_CODE:
        show_result(codes_set_user(command->code));
        break;
    case COMMAND_SET_ADMIN_CODE:
        show_result(codes_set_admin(command->code));
        break;
    case COMMAND_BUILDING_RESET:
        building_reset();
        break;
    }
}

/* Runs in the db_proto task. */
static void on_message(const uint8_t src[6], const db_msg_t *msg)
{
    uint8_t apartment;
    registry_entry_t entry;
    if (msg->opcode != DB_OP_OPEN_DOOR || !registry_find(src, &apartment, &entry) || apartment != msg->apartment ||
            msg->session != entry.session) {
        return;  /* only registered modules, for their own apartment number, in the current session */
    }
    switch (db_proto_check_counter(src, msg->counter)) {
    case DB_REPLAY_NEW:
        ESP_LOGI(TAG, "door-open request from apartment %u", apartment);
        door_open();
        db_proto_ack(src, DB_OP_OPEN_DOOR_ACK, msg);
        break;
    case DB_REPLAY_RETRY:
        db_proto_ack(src, DB_OP_OPEN_DOOR_ACK, msg);  /* a retry: acknowledge again, do not open twice */
        break;
    case DB_REPLAY_OLD:
        ESP_LOGW(TAG, "old OPEN_DOOR counter %u dropped", (unsigned)msg->counter);
        break;
    }
}

static void app_task(void *arg)
{
    app_event_t event;
    for (;;) {
        if (xQueueReceive(s_events, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (event.type) {
        case EVENT_KEY:
            input_key(event.key);
            break;
        case EVENT_INPUT_TIMEOUT:
            input_timeout();
            break;
        case EVENT_CODE_RESET:
            ESP_LOGI(TAG, "codes reset to the factory codes");
            show_result(codes_reset());
            break;
        }
    }
}

/* SPEC 7.5: use the stored channel; without one, run the site scan and store the result. */
static esp_err_t setup_channel(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    uint8_t channel = 0;
    esp_err_t err = nvs_get_u8(nvs, KEY_CHANNEL, &channel);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = db_net_scan_best_channel(&channel);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "site scan selected channel %u", channel);
            err = nvs_set_u8(nvs, KEY_CHANNEL, channel);
            if (err == ESP_OK) {
                err = nvs_commit(nvs);
            }
        } else {
            /* Keep working on the first candidate; nothing is stored, so the next boot scans again. */
            uint8_t candidates[DB_CHANNEL_CANDIDATE_COUNT];
            db_net_channel_candidates(candidates);
            ESP_LOGE(TAG, "site scan failed (%s); using channel %u until the next boot", esp_err_to_name(err),
                     candidates[0]);
            channel = candidates[0];
            err = ESP_OK;
        }
    }
    nvs_close(nvs);
    ESP_RETURN_ON_ERROR(err, TAG, "channel setup failed");
    return db_net_set_channel(channel);
}

static esp_err_t reset_button_init(void)
{
    const button_config_t config = { 0 };
    const button_gpio_config_t gpio = {
        .gpio_num = CONFIG_DB_CODE_RESET_BUTTON_GPIO,
        .active_level = RESET_ACTIVE_LEVEL,
        .disable_pull = RESET_DISABLE_PULL,
    };
    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&config, &gpio, &s_reset_button), TAG, "button create failed");
    return iot_button_register_cb(s_reset_button, BUTTON_PRESS_UP, NULL, on_reset_released, NULL);
}

void app_main(void)
{
    s_events = xQueueCreate(APP_QUEUE_LENGTH, sizeof(app_event_t));
    ESP_ERROR_CHECK(s_events ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(db_net_init());
    ESP_ERROR_CHECK(db_status_led_init(CONFIG_DB_STATUS_LED_GPIO));
    ESP_ERROR_CHECK(door_init());
    ESP_ERROR_CHECK(codes_init());
    ESP_ERROR_CHECK(registry_init());
    ESP_ERROR_CHECK(setup_channel());
    ESP_ERROR_CHECK(db_proto_init(on_message));
    ESP_ERROR_CHECK(input_init(on_command, on_input_timeout));
    ESP_ERROR_CHECK(xTaskCreate(app_task, "panel", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(keypad_init(on_key));
    ESP_ERROR_CHECK(reset_button_init());
    ESP_LOGI(TAG, "started on channel %u", db_net_get_channel());
}
