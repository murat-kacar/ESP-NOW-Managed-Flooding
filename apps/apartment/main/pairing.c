#include "pairing.h"

#include <string.h>

#include "db_net.h"
#include "db_status_led.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "espnow.h"
#include "espnow_security.h"
#include "espnow_security_handshake.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "pairing";

#define DISCOVERY_TASK_STACK 3072
#define DISCOVERY_TASK_PRIO  4

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_window_timer;
static TaskHandle_t s_discovery_task;
static bool s_active;
static bool s_found;            /* the panel answered our HELLO: stay on this channel */
static bool s_handshake_done;
static uint8_t s_panel_mac[DB_MAC_LEN];  /* from HELLO_ACK, confirmed by ESP_EVENT_ESPNOW_SEC_OK */
static uint8_t s_panel_channel;          /* channel the HELLO_ACK was received on */
static uint8_t s_channels[DB_CHANNEL_CANDIDATE_COUNT];

static bool is_active(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = s_active && !s_found;
    xSemaphoreGive(s_lock);
    return active;
}

/* One task per window: HELLO on each channel, then wait for the panel's answer (ADR 0012). */
static void discovery_task(void *arg)
{
    for (int i = 0; is_active(); i = (i + 1) % DB_CHANNEL_CANDIDATE_COUNT) {
        if (ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_set_channel(s_channels[i])) == ESP_OK) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_send_hello(ESPNOW_ADDR_BROADCAST, DB_OP_PAIR_HELLO));
        }
        /* Woken early by HELLO_ACK or by the end of the window. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(CONFIG_DB_CHANNEL_DWELL_MS));
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool found = s_found;
    const uint8_t channel = s_panel_channel;
    s_discovery_task = NULL;
    xSemaphoreGive(s_lock);
    if (found) {
        /* The HELLO_ACK may be processed after this task already moved to the next channel: stay on the channel
         * the answer was received on, not on the channel the loop ended on. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_set_channel(channel));
        ESP_LOGI(TAG, "panel " MACSTR " answered on channel %u", MAC2STR(s_panel_mac), channel);
    }
    vTaskDelete(NULL);
}

void pairing_on_hello(const uint8_t src[DB_MAC_LEN], db_opcode_t opcode, uint8_t channel)
{
    if (opcode != DB_OP_PAIR_HELLO_ACK) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active && !s_found) {
        s_found = true;
        s_panel_channel = channel;
        memcpy(s_panel_mac, src, DB_MAC_LEN);
        if (s_discovery_task) {
            xTaskNotifyGive(s_discovery_task);
        }
    }
    xSemaphoreGive(s_lock);
}

static void window_expired(void *arg)
{
    ESP_LOGI(TAG, "pairing window expired");
    pairing_finish(false);
}

static void on_espnow_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ESP_EVENT_ESPNOW_SEC_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        /* Only the panel that answered our HELLO may complete the pairing. */
        if (s_active && s_found && memcmp(data, s_panel_mac, DB_MAC_LEN) == 0) {
            s_handshake_done = true;
            ESP_LOGI(TAG, "handshake with panel " MACSTR " on channel %u", MAC2STR(s_panel_mac),
                     db_net_get_channel());
        }
        xSemaphoreGive(s_lock);
    } else if (id == ESP_EVENT_ESPNOW_SEC_FAIL) {
        ESP_LOGW(TAG, "handshake failed with " MACSTR, MAC2STR((const uint8_t *)data));
    }
}

esp_err_t pairing_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "no memory");
    db_net_channel_candidates(s_channels);
    const esp_timer_create_args_t window_args = { .callback = window_expired, .name = "pair_window" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&window_args, &s_window_timer), TAG, "timer create failed");
    return esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, on_espnow_event, NULL);
}

esp_err_t pairing_start(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active || s_discovery_task) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_active = true;
    s_found = false;
    s_handshake_done = false;
    xSemaphoreGive(s_lock);

    esp_err_t err = espnow_sec_responder_start(DB_SEC_POP);
    if (err == ESP_OK) {
        err = esp_timer_start_once(s_window_timer, (uint64_t)CONFIG_DB_PAIRING_WINDOW_S * 1000000U);
    }
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (xTaskCreate(discovery_task, "pair_discovery", DISCOVERY_TASK_STACK, NULL, DISCOVERY_TASK_PRIO,
                        &s_discovery_task) != pdPASS) {
            err = ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(s_lock);
    }
    if (err != ESP_OK) {
        pairing_finish(false);
        return err;
    }
    db_status_led_start(DB_LED_PAIRING_MODE);
    ESP_LOGI(TAG, "pairing window open for %d s", CONFIG_DB_PAIRING_WINDOW_S);
    return ESP_OK;
}

bool pairing_is_expected_panel(const uint8_t mac[DB_MAC_LEN])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool expected = s_active && s_handshake_done && memcmp(mac, s_panel_mac, DB_MAC_LEN) == 0;
    xSemaphoreGive(s_lock);
    return expected;
}

void pairing_finish(bool success)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool was_active = s_active;
    s_active = false;
    s_found = false;
    s_handshake_done = false;
    if (s_discovery_task) {
        xTaskNotifyGive(s_discovery_task);  /* ends the discovery loop */
    }
    xSemaphoreGive(s_lock);
    if (!was_active) {
        return;
    }

    (void)esp_timer_stop(s_window_timer);  /* not running when the window expired */
    ESP_ERROR_CHECK_WITHOUT_ABORT(espnow_sec_responder_stop());
    db_status_led_stop(DB_LED_PAIRING_MODE);
    if (!success) {
        /* An unpaired module holds no key. The responder stores the key of whichever initiator completed the
         * handshake first, even one that is not the panel that answered our HELLO, so erase it unconditionally. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(db_net_erase_key());
    }
}
