#include "db_net.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "espnow.h"
#include "espnow_storage.h"

static const char *TAG = "db_net";

/* Order follows ESP-IDF examples/wifi/espnow and esp-now examples/get-started. */
static esp_err_t wifi_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop create failed");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage failed");
    /* Every node forwards packets, so the radio must stay on (SPEC 7.1). */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "set power save failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    return ESP_OK;
}

esp_err_t db_net_init(void)
{
    /* Initialises the default NVS partition. */
    ESP_RETURN_ON_ERROR(espnow_storage_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(wifi_init(), TAG, "wifi init failed");

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(espnow_init(&espnow_config), TAG, "espnow_init failed");

    return ESP_OK;
}
