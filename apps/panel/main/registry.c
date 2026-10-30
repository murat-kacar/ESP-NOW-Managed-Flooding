#include "registry.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "registry";

#define NVS_NAMESPACE  "db_registry"
#define APARTMENT_SLOTS 256  /* apartment numbers are 8 bits */

typedef struct {
    bool used;
    uint8_t mac[6];
} entry_t;

static entry_t s_entries[APARTMENT_SLOTS];
static SemaphoreHandle_t s_lock;

static void key_for(uint8_t apartment, char key[8])
{
    snprintf(key, 8, "apt%u", apartment);
}

esp_err_t registry_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "no memory");
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return ESP_OK;  /* nothing registered yet */
    }
    int count = 0;
    for (int apartment = CONFIG_DB_APARTMENT_MIN; apartment <= CONFIG_DB_APARTMENT_MAX; apartment++) {
        char key[8];
        key_for(apartment, key);
        size_t size = sizeof(s_entries[apartment].mac);
        if (nvs_get_blob(nvs, key, s_entries[apartment].mac, &size) == ESP_OK && size == 6) {
            s_entries[apartment].used = true;
            count++;
        }
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "%d apartment(s) registered", count);
    return ESP_OK;
}

bool registry_get(uint8_t apartment, uint8_t mac[6])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool used = s_entries[apartment].used;
    if (used) {
        memcpy(mac, s_entries[apartment].mac, 6);
    }
    xSemaphoreGive(s_lock);
    return used;
}

bool registry_find(const uint8_t mac[6], uint8_t *apartment)
{
    bool found = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < APARTMENT_SLOTS && !found; i++) {
        if (s_entries[i].used && memcmp(s_entries[i].mac, mac, 6) == 0) {
            *apartment = (uint8_t)i;
            found = true;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}

static esp_err_t nvs_update(uint8_t apartment, const uint8_t *mac)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    char key[8];
    key_for(apartment, key);
    esp_err_t err = mac ? nvs_set_blob(nvs, key, mac, 6) : nvs_erase_key(nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t registry_add(uint8_t apartment, const uint8_t mac[6])
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = s_entries[apartment].used ? ESP_ERR_INVALID_STATE : nvs_update(apartment, mac);
    if (err == ESP_OK) {
        s_entries[apartment].used = true;
        memcpy(s_entries[apartment].mac, mac, 6);
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t registry_remove(uint8_t apartment)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = nvs_update(apartment, NULL);
    if (err == ESP_OK) {
        s_entries[apartment].used = false;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t registry_erase_all(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    esp_err_t err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(s_entries, 0, sizeof(s_entries));
    xSemaphoreGive(s_lock);
    return err;
}
