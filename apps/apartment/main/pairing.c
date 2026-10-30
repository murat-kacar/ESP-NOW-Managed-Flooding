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

static const char *TAG = "pairing";

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_hop_timer;
static esp_timer_handle_t s_window_timer;
static bool s_active;
static bool s_handshake_done;
static uint8_t s_panel_mac[6];
static uint8_t s_channels[DB_CHANNEL_CANDIDATE_COUNT];
static int s_channel_index;

static void hop(void *arg)
{
    s_channel_index = (s_channel_index + 1) % DB_CHANNEL_CANDIDATE_COUNT;
    db_net_set_channel(s_channels[s_channel_index]);
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
        if (s_active) {
            /* Stay on the channel where the panel was found; PAIR_CONFIG arrives here. */
            esp_timer_stop(s_hop_timer);
            memcpy(s_panel_mac, data, sizeof(s_panel_mac));
            s_handshake_done = true;
            ESP_LOGI(TAG, "handshake with panel " MACSTR " on channel %u", MAC2STR(s_panel_mac), db_net_get_channel());
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

    const esp_timer_create_args_t hop_args = { .callback = hop, .name = "pair_hop" };
    const esp_timer_create_args_t window_args = { .callback = window_expired, .name = "pair_window" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&hop_args, &s_hop_timer), TAG, "timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_create(&window_args, &s_window_timer), TAG, "timer create failed");
    return esp_event_handler_register(ESP_EVENT_ESPNOW, ESP_EVENT_ANY_ID, on_espnow_event, NULL);
}

esp_err_t pairing_start(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_active) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_active = true;
    s_handshake_done = false;
    s_channel_index = 0;
    xSemaphoreGive(s_lock);

    db_net_set_channel(s_channels[0]);
    esp_err_t err = espnow_sec_responder_start(DB_SEC_POP);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(s_hop_timer, (uint64_t)CONFIG_DB_CHANNEL_DWELL_MS * 1000U);
    }
    if (err == ESP_OK) {
        err = esp_timer_start_once(s_window_timer, (uint64_t)CONFIG_DB_PAIRING_WINDOW_S * 1000000U);
    }
    if (err != ESP_OK) {
        pairing_finish(false);
        return err;
    }
    db_status_led_start(DB_LED_PAIRING_MODE);
    ESP_LOGI(TAG, "pairing window open for %d s", CONFIG_DB_PAIRING_WINDOW_S);
    return ESP_OK;
}

bool pairing_is_expected_panel(const uint8_t mac[6])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool expected = s_active && s_handshake_done && memcmp(mac, s_panel_mac, sizeof(s_panel_mac)) == 0;
    xSemaphoreGive(s_lock);
    return expected;
}

void pairing_finish(bool success)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool was_active = s_active;
    bool handshake_done = s_handshake_done;
    s_active = false;
    s_handshake_done = false;
    xSemaphoreGive(s_lock);
    if (!was_active) {
        return;
    }

    esp_timer_stop(s_hop_timer);
    esp_timer_stop(s_window_timer);
    espnow_sec_responder_stop();
    db_status_led_stop(DB_LED_PAIRING_MODE);
    if (!success && handshake_done) {
        /* A configured responder ignores new handshakes, so drop the key from the incomplete pairing. */
        db_net_erase_key();
    }
}
