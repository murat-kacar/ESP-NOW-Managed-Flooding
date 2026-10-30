#include "db_net.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_security.h"
#include "espnow_storage.h"
#include "nvs.h"

static const char *TAG = "db_net";

/* esp_wifi_set_max_tx_power() unit is 0.25 dBm. ESP-IDF maps 40 to 34 (8.5 dBm) and 80 to 80 (20 dBm). */
#if CONFIG_DB_TX_POWER_10DBM
#define TX_POWER_QUARTER_DBM 40
#else
#define TX_POWER_QUARTER_DBM 80
#endif

/* Use the key for encryption and decryption; esp-now stores it (esp-now examples/security). */
static esp_err_t install_key(uint8_t key_info[APP_KEY_LEN])
{
    ESP_RETURN_ON_ERROR(espnow_set_key(key_info), TAG, "set key failed");
    return espnow_set_dec_key(key_info);
}

/* Order follows ESP-IDF examples/wifi/espnow and esp-now examples/get-started. */
static esp_err_t wifi_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop create failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage failed");
#if CONFIG_DB_WIFI_COUNTRY_OVERRIDE
    /* 802.11d off: there is no AP to learn the country from. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code(CONFIG_DB_WIFI_COUNTRY_CODE, false), TAG, "set country failed");
#endif
    /* Every node forwards packets, so the radio must stay on (SPEC 7.1). */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "set power save failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    /* Must be called after esp_wifi_start() (esp_wifi.h). */
    ESP_RETURN_ON_ERROR(esp_wifi_set_max_tx_power(TX_POWER_QUARTER_DBM), TAG, "set TX power failed");
#if CONFIG_DB_WIFI_LONG_RANGE
    ESP_RETURN_ON_ERROR(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                              WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR), TAG, "set protocol failed");
#endif
    return ESP_OK;
}

esp_err_t db_net_init(void)
{
    /* Initialises the default NVS partition. */
    ESP_RETURN_ON_ERROR(espnow_storage_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(wifi_init(), TAG, "wifi init failed");

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_config.sec_enable = 1;  /* esp-now examples/security */
    ESP_RETURN_ON_ERROR(espnow_init(&espnow_config), TAG, "espnow_init failed");

    uint8_t key_info[APP_KEY_LEN];
    bool has_key = espnow_get_key(key_info) == ESP_OK;
    esp_err_t err = has_key ? install_key(key_info) : ESP_OK;
    memset(key_info, 0, sizeof(key_info));
    ESP_RETURN_ON_ERROR(err, TAG, "loading the stored key failed");
    ESP_LOGI(TAG, "TX power %d (0.25 dBm units), application key %s", TX_POWER_QUARTER_DBM,
             has_key ? "loaded" : "not set");
    return ESP_OK;
}

esp_err_t db_net_set_channel(uint8_t channel)
{
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

uint8_t db_net_get_channel(void)
{
    uint8_t primary = 0;
    wifi_second_chan_t second;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_channel(&primary, &second));  /* 0 on failure */
    return primary;
}

void db_net_channel_candidates(uint8_t out[DB_CHANNEL_CANDIDATE_COUNT])
{
    out[0] = CONFIG_DB_CHANNEL_CANDIDATE_1;
    out[1] = CONFIG_DB_CHANNEL_CANDIDATE_2;
    out[2] = CONFIG_DB_CHANNEL_CANDIDATE_3;
}

esp_err_t db_net_scan_best_channel(uint8_t *channel)
{
    const wifi_scan_config_t scan = { .show_hidden = true };  /* channel 0: all channels */
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan, true), TAG, "scan failed");

    uint16_t count = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&count), TAG, "scan result failed");
    wifi_ap_record_t *records = count ? calloc(count, sizeof(*records)) : NULL;
    ESP_RETURN_ON_FALSE(count == 0 || records, ESP_ERR_NO_MEM, TAG, "no memory for %u records", count);
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);  /* also frees the driver's list */
    if (err != ESP_OK) {
        free(records);
        return err;
    }

    uint8_t candidates[DB_CHANNEL_CANDIDATE_COUNT];
    db_net_channel_candidates(candidates);
    int best = 0;
    int best_aps = INT_MAX;
    int best_rssi = INT_MAX;
    for (int i = 0; i < DB_CHANNEL_CANDIDATE_COUNT; i++) {
        int aps = 0;
        int max_rssi = INT_MIN;
        for (int r = 0; r < count; r++) {
            if (records[r].primary == candidates[i]) {
                aps++;
                max_rssi = records[r].rssi > max_rssi ? records[r].rssi : max_rssi;
            }
        }
        ESP_LOGI(TAG, "channel %u: %d AP(s), max RSSI %d", candidates[i], aps, aps ? max_rssi : 0);
        if (aps < best_aps || (aps == best_aps && max_rssi < best_rssi)) {
            best = i;
            best_aps = aps;
            best_rssi = max_rssi;
        }
    }
    free(records);
    *channel = candidates[best];
    return ESP_OK;
}

bool db_net_has_key(void)
{
    /* esp-now owns the key state: an espnow_sec responder stores the key itself after the handshake. */
    uint8_t key_info[APP_KEY_LEN];
    bool has_key = espnow_get_key(key_info) == ESP_OK;
    memset(key_info, 0, sizeof(key_info));
    return has_key;
}

esp_err_t db_net_ensure_key(void)
{
    if (db_net_has_key()) {
        return ESP_OK;
    }
    uint8_t key_info[APP_KEY_LEN];
    esp_fill_random(key_info, sizeof(key_info));
    esp_err_t err = install_key(key_info);
    memset(key_info, 0, sizeof(key_info));
    return err;
}

esp_err_t db_net_erase_key(void)
{
    esp_err_t err = espnow_erase_key();
    esp_err_t dec = espnow_erase_dec_key();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    return (dec == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : dec;
}
