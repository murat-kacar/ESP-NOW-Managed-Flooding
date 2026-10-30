#include "identity.h"

#include "db_nvs.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "identity";

#define NVS_NAMESPACE "db_identity"
#define NVS_KEY       "identity"

bool identity_load(identity_t *out)
{
    esp_err_t err = db_nvs_get_blob(NVS_NAMESPACE, NVS_KEY, out, sizeof(*out));
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "stored identity unreadable (%s): treated as unpaired", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

esp_err_t identity_save(const identity_t *identity)
{
    return db_nvs_set_blob(NVS_NAMESPACE, NVS_KEY, identity, sizeof(*identity));
}

esp_err_t identity_erase(void)
{
    return db_nvs_erase_namespace(NVS_NAMESPACE);
}
