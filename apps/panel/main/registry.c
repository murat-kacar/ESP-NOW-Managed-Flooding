#include "registry.h"

#include <stdio.h>
#include <string.h>

#include "db_nvs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "registry";

#define NVS_NAMESPACE   "db_registry"
#define APARTMENT_SLOTS 256  /* apartment numbers are 8 bits */
#define KEY_SIZE        8    /* "apt255" and the terminator */

typedef struct {
    bool used;
    registry_entry_t entry;
} slot_t;

static slot_t s_slots[APARTMENT_SLOTS];
static SemaphoreHandle_t s_lock;

static void key_for(uint8_t apartment, char key[KEY_SIZE])
{
    snprintf(key, KEY_SIZE, "apt%u", apartment);
}

esp_err_t registry_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "no memory");
    int count = 0;
    for (int apartment = CONFIG_DB_APARTMENT_MIN; apartment <= CONFIG_DB_APARTMENT_MAX; apartment++) {
        char key[KEY_SIZE];
        key_for(apartment, key);
        registry_entry_t entry;
        esp_err_t err = db_nvs_get_blob(NVS_NAMESPACE, key, &entry, sizeof(entry));
        if (err == ESP_OK) {
            s_slots[apartment] = (slot_t) { .used = true, .entry = entry };
            count++;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "apartment %d record unreadable (%s): ignored", apartment, esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "%d apartment(s) registered", count);
    return ESP_OK;
}

bool registry_get(uint8_t apartment, registry_entry_t *entry)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool used = s_slots[apartment].used;
    if (used) {
        *entry = s_slots[apartment].entry;
    }
    xSemaphoreGive(s_lock);
    return used;
}

bool registry_find(const uint8_t mac[DB_MAC_LEN], uint8_t *apartment, registry_entry_t *entry)
{
    bool found = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < APARTMENT_SLOTS && !found; i++) {
        if (s_slots[i].used && memcmp(s_slots[i].entry.mac, mac, DB_MAC_LEN) == 0) {
            *apartment = (uint8_t)i;
            *entry = s_slots[i].entry;
            found = true;
        }
    }
    xSemaphoreGive(s_lock);
    return found;
}

esp_err_t registry_add(uint8_t apartment, const registry_entry_t *entry)
{
    char key[KEY_SIZE];
    key_for(apartment, key);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = s_slots[apartment].used ? ESP_ERR_INVALID_STATE
                                             : db_nvs_set_blob(NVS_NAMESPACE, key, entry, sizeof(*entry));
    if (err == ESP_OK) {
        s_slots[apartment] = (slot_t) { .used = true, .entry = *entry };
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t registry_remove(uint8_t apartment)
{
    char key[KEY_SIZE];
    key_for(apartment, key);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = db_nvs_erase_key(NVS_NAMESPACE, key);
    if (err == ESP_OK) {
        s_slots[apartment].used = false;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t registry_erase_all(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = db_nvs_erase_namespace(NVS_NAMESPACE);
    if (err == ESP_OK) {
        memset(s_slots, 0, sizeof(s_slots));
    }
    xSemaphoreGive(s_lock);
    return err;
}
