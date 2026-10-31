#include "pairing.h"

#include <string.h>

#include "db_net.h"
#include "db_proto.h"
#include "db_status_led.h"
#include "db_wdt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "espnow.h"
#include "espnow_security.h"
#include "espnow_security_handshake.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "health.h"
#include "registry.h"

static const char *TAG = "pairing";

#define CONFIG_RETRIES 2  /* PAIR_CONFIG attempts after the first, like RING (SPEC 7.4) */
#define MAX_MODULES    2  /* enough to detect "more than one" */
/* After the first HELLO, listen for one full channel cycle of the module so a second module is noticed. */
#define COLLECT_MS     (DB_CHANNEL_CANDIDATE_COUNT * CONFIG_DB_CHANNEL_DWELL_MS)

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_hello_seen;
static bool s_window_open;
static uint8_t s_modules[MAX_MODULES][DB_MAC_LEN];
static int s_module_count;

esp_err_t pairing_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_hello_seen = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lock && s_hello_seen, ESP_ERR_NO_MEM, TAG, "no memory");
    return ESP_OK;
}

void pairing_on_hello(const uint8_t src[DB_MAC_LEN], db_opcode_t opcode)
{
    if (opcode != DB_OP_PAIR_HELLO) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_window_open) {
        bool known = false;
        for (int i = 0; i < s_module_count; i++) {
            known |= memcmp(s_modules[i], src, DB_MAC_LEN) == 0;
        }
        if (!known && s_module_count < MAX_MODULES) {
            memcpy(s_modules[s_module_count++], src, DB_MAC_LEN);
        }
        xSemaphoreGive(s_hello_seen);
    }
    xSemaphoreGive(s_lock);
}

static int module_count(uint8_t first[DB_MAC_LEN])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int count = s_module_count;
    if (count > 0) {
        memcpy(first, s_modules[0], DB_MAC_LEN);
    }
    xSemaphoreGive(s_lock);
    return count;
}

static void set_window(bool open)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_window_open = open;
    s_module_count = 0;
    xSemaphoreGive(s_lock);
    xSemaphoreTake(s_hello_seen, 0);
}

static TickType_t ticks_until(int64_t deadline_us)
{
    int64_t remaining_us = deadline_us - esp_timer_get_time();
    return remaining_us > 0 ? pdMS_TO_TICKS(remaining_us / 1000) : 0;
}

static esp_err_t handshake(const uint8_t mac[DB_MAC_LEN])
{
    ESP_RETURN_ON_ERROR(db_net_ensure_key(), TAG, "no application key");
    uint8_t key_info[APP_KEY_LEN];
    ESP_RETURN_ON_ERROR(espnow_get_key(key_info), TAG, "reading the key failed");

    uint8_t addrs[1][DB_MAC_LEN];
    memcpy(addrs[0], mac, DB_MAC_LEN);
    espnow_sec_result_t result = { 0 };
    esp_err_t err = espnow_sec_initiator_start(key_info, DB_SEC_POP, addrs, 1, &result);
    memset(key_info, 0, sizeof(key_info));
    if (err == ESP_OK && result.successed_num != 1) {
        err = ESP_FAIL;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(espnow_sec_initiator_result_free(&result));
    return err;
}

/* The pairing runs in the supervised application task: long waits are cut into watchdog feed periods
 * (ADR 0016). */
static bool take_supervised(SemaphoreHandle_t semaphore, int64_t deadline_us)
{
    for (;;) {
        db_wdt_feed();
        TickType_t left = ticks_until(deadline_us);
        if (xSemaphoreTake(semaphore, left < DB_WDT_FEED_TICKS ? left : DB_WDT_FEED_TICKS) == pdTRUE) {
            return true;
        }
        if (ticks_until(deadline_us) == 0) {
            return false;
        }
    }
}

static void delay_supervised(uint32_t ms)
{
    const int64_t until_us = esp_timer_get_time() + (int64_t)ms * 1000;
    for (TickType_t left = ticks_until(until_us); left > 0; left = ticks_until(until_us)) {
        db_wdt_feed();
        vTaskDelay(left < DB_WDT_FEED_TICKS ? left : DB_WDT_FEED_TICKS);
    }
}

/* Discovery and handshake inside the window; retries the handshake until the window ends. */
static esp_err_t find_and_handshake(uint8_t mac[DB_MAC_LEN])
{
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)CONFIG_DB_PAIRING_WINDOW_S * 1000000;
    if (!take_supervised(s_hello_seen, deadline_us)) {
        return ESP_ERR_NOT_FOUND;
    }
    delay_supervised(COLLECT_MS);
    if (module_count(mac) > 1) {
        ESP_LOGW(TAG, "more than one unpaired module answered: pairing cancelled (ADR 0007)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "module " MACSTR " found", MAC2STR(mac));

    esp_err_t err = ESP_ERR_NOT_FOUND;
    while (esp_timer_get_time() < deadline_us) {
        db_wdt_feed();  /* one pass: a handshake (about 3 s) and at most one dwell */
        /* The module stays on this channel; a lost HELLO_ACK makes this handshake fail and is retried. */
        ESP_ERROR_CHECK_WITHOUT_ABORT(db_proto_send_hello(mac, DB_OP_PAIR_HELLO_ACK));
        err = handshake(mac);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "handshake failed (%s); retrying in the window", esp_err_to_name(err));
        /* A module that got HELLO_ACK stays and sends no more HELLOs; one that missed it sends the next HELLO
         * within one dwell. Either way retry after at most one dwell. */
        TickType_t wait = pdMS_TO_TICKS(CONFIG_DB_CHANNEL_DWELL_MS);
        TickType_t left = ticks_until(deadline_us);
        xSemaphoreTake(s_hello_seen, left < wait ? left : wait);
    }
    return err;
}

esp_err_t pairing_run(uint8_t apartment)
{
    registry_entry_t entry;
    if (registry_get(apartment, &entry)) {
        ESP_LOGW(TAG, "apartment %u is already registered (SPEC 5.2)", apartment);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "pairing window open for apartment %u", apartment);
    db_status_led_start(DB_LED_PAIRING_WINDOW);
    set_window(true);
    esp_err_t err = find_and_handshake(entry.mac);
    set_window(false);
    db_status_led_stop(DB_LED_PAIRING_WINDOW);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pairing failed: %s", esp_err_to_name(err));
        return err;
    }

    /* A new session invalidates every message of an earlier pairing of this MAC, so its counter can restart
     * (a reset or reflashed module counts from 1 again). */
    entry.session = esp_random();
    ESP_RETURN_ON_ERROR(db_proto_forget_peer(entry.mac), TAG, "forgetting the old counter failed");
    db_msg_t config = {
        .opcode = DB_OP_PAIR_CONFIG, .apartment = apartment, .channel = db_net_get_channel(), .session = entry.session,
    };
    ESP_RETURN_ON_ERROR(db_proto_request(entry.mac, &config, DB_OP_PAIR_CONFIG_ACK, CONFIG_RETRIES), TAG,
                        "configuration message not acknowledged");
    ESP_RETURN_ON_ERROR(registry_add(apartment, &entry), TAG, "registering failed");
    health_on_paired(apartment);
    ESP_LOGI(TAG, "apartment %u paired", apartment);
    return ESP_OK;
}
