#include "identity.h"

#include "esp_check.h"
#include "nvs.h"

static const char *TAG = "identity";

#define NVS_NAMESPACE "db_identity"
#define NVS_KEY       "identity"

bool identity_load(identity_t *out)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*out);
    esp_err_t err = nvs_get_blob(nvs, NVS_KEY, out, &size);
    nvs_close(nvs);
    return err == ESP_OK && size == sizeof(*out);
}

esp_err_t identity_save(const identity_t *identity)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    esp_err_t err = nvs_set_blob(nvs, NVS_KEY, identity, sizeof(*identity));
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t identity_erase(void)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs_open failed");
    esp_err_t err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}
