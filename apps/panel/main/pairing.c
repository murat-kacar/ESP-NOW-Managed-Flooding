#include "pairing.h"

#include <string.h>

#include "db_net.h"
#include "db_proto.h"
#include "db_status_led.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "espnow.h"
#include "espnow_security.h"
#include "espnow_security_handshake.h"
#include "registry.h"

static const char *TAG = "pairing";

/* espnow_sec_initiator_scan() sends a request every 500 ms until wait_ticks pass (at most 5). A short scan leaves
 * most of the module's channel dwell (DB_CHANNEL_DWELL_MS) for the handshake before it hops away. */
#define SCAN_MS               1000
_Static_assert(CONFIG_DB_CHANNEL_DWELL_MS > 2 * SCAN_MS,
               "DB_CHANNEL_DWELL_MS must leave time for the handshake after a panel scan");
#define CONFIG_RETRIES        2     /* PAIR_CONFIG attempts after the first, like RING (SPEC 7.4) */

/* Scan until exactly one unpaired module answers or the window ends. */
static esp_err_t find_module(uint8_t mac[6])
{
    const int64_t deadline_us = esp_timer_get_time() + (int64_t)CONFIG_DB_PAIRING_WINDOW_S * 1000000;
    while (esp_timer_get_time() < deadline_us) {
        int64_t remaining_ms = (deadline_us - esp_timer_get_time()) / 1000;
        int64_t scan_ms = remaining_ms < SCAN_MS ? remaining_ms : SCAN_MS;
        espnow_sec_responder_t *list = NULL;
        size_t count = 0;
        ESP_RETURN_ON_ERROR(espnow_sec_initiator_scan(&list, &count, pdMS_TO_TICKS(scan_ms)), TAG, "scan failed");

        size_t unpaired = 0;
        for (size_t i = 0; i < count; i++) {
            if (list[i].sec_ver == ESPNOW_SEC_VER_NONE) {  /* a configured responder reports its version */
                memcpy(mac, list[i].mac, 6);
                unpaired++;
            }
        }
        espnow_sec_initiator_scan_result_free();
        if (unpaired > 1) {
            ESP_LOGW(TAG, "%u unpaired modules answered: pairing cancelled (ADR 0007)", (unsigned)unpaired);
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (unpaired == 1) {
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t handshake(const uint8_t mac[6])
{
    ESP_RETURN_ON_ERROR(db_net_ensure_key(), TAG, "no application key");
    uint8_t key_info[APP_KEY_LEN];
    ESP_RETURN_ON_ERROR(espnow_get_key(key_info), TAG, "reading the key failed");

    uint8_t addrs[1][6];
    memcpy(addrs[0], mac, 6);
    espnow_sec_result_t result = { 0 };
    esp_err_t err = espnow_sec_initiator_start(key_info, DB_SEC_POP, addrs, 1, &result);
    if (err == ESP_OK && result.successed_num != 1) {
        err = ESP_FAIL;
    }
    espnow_sec_initiator_result_free(&result);
    return err;
}

esp_err_t pairing_run(uint8_t apartment)
{
    uint8_t mac[6];
    if (registry_get(apartment, mac)) {
        ESP_LOGW(TAG, "apartment %u is already registered (SPEC 5.2)", apartment);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "pairing window open for apartment %u", apartment);
    db_status_led_start(DB_LED_PAIRING_WINDOW);
    esp_err_t err = find_module(mac);
    db_status_led_stop(DB_LED_PAIRING_WINDOW);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "module " MACSTR " found", MAC2STR(mac));
    ESP_RETURN_ON_ERROR(handshake(mac), TAG, "handshake failed");

    db_msg_t config = { .opcode = DB_OP_PAIR_CONFIG, .apartment = apartment, .channel = db_net_get_channel() };
    ESP_RETURN_ON_ERROR(db_proto_request(mac, &config, DB_OP_PAIR_CONFIG_ACK, CONFIG_RETRIES), TAG,
                        "configuration message not acknowledged");
    ESP_RETURN_ON_ERROR(registry_add(apartment, mac), TAG, "registering failed");
    ESP_LOGI(TAG, "apartment %u paired", apartment);
    return ESP_OK;
}
