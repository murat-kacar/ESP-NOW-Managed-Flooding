#include "db_nvs.h"

#include "nvs.h"

static esp_err_t commit_and_close(nvs_handle_t nvs, esp_err_t err)
{
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t db_nvs_get_u8(const char *ns, const char *key, uint8_t *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u8(nvs, key, value);
    nvs_close(nvs);
    return err;
}

esp_err_t db_nvs_set_u8(const char *ns, const char *key, uint8_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    return commit_and_close(nvs, nvs_set_u8(nvs, key, value));
}

esp_err_t db_nvs_get_u32(const char *ns, const char *key, uint32_t *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_u32(nvs, key, value);
    nvs_close(nvs);
    return err;
}

esp_err_t db_nvs_set_u32(const char *ns, const char *key, uint32_t value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    return commit_and_close(nvs, nvs_set_u32(nvs, key, value));
}

esp_err_t db_nvs_get_blob(const char *ns, const char *key, void *data, size_t size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    size_t stored = 0;
    err = nvs_get_blob(nvs, key, NULL, &stored);  /* NULL buffer: query the stored length */
    if (err == ESP_OK && stored != size) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) {
        err = nvs_get_blob(nvs, key, data, &size);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t db_nvs_set_blob(const char *ns, const char *key, const void *data, size_t size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    return commit_and_close(nvs, nvs_set_blob(nvs, key, data, size));
}

esp_err_t db_nvs_set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    return commit_and_close(nvs, nvs_set_str(nvs, key, value));
}

esp_err_t db_nvs_erase_key(const char *ns, const char *key)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_key(nvs, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    return commit_and_close(nvs, err);
}

esp_err_t db_nvs_erase_namespace(const char *ns)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    return commit_and_close(nvs, nvs_erase_all(nvs));
}
