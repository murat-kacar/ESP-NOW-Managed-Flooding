#include "codes.h"

#include <string.h>

#include "db_nvs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "codes";

#define FACTORY_PARTITION "fctry"
#define FACTORY_NAMESPACE "factory"
#define USER_NAMESPACE    "db_codes"
#define KEY_ADMIN         "admin_code"
#define KEY_USER          "user_code"
#define MAX_CODE_LENGTH   16

static bool s_factory_available;

esp_err_t codes_init(void)
{
    esp_err_t err = nvs_flash_init_partition(FACTORY_PARTITION);
    s_factory_available = err == ESP_OK;
    if (!s_factory_available) {
        ESP_LOGE(TAG, "factory partition not available (%s): flash the factory NVS with the flasher",
                 esp_err_to_name(err));
    }
    return ESP_OK;  /* the panel still rings without codes */
}

static bool read_code(const char *partition, const char *ns, const char *key, char *out, size_t size)
{
    nvs_handle_t nvs;
    if (nvs_open_from_partition(partition, ns, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_str(nvs, key, out, &size);
    nvs_close(nvs);
    return err == ESP_OK;
}

/* Changed code from the normal NVS, else the factory code. */
static bool effective_code(const char *key, char *out, size_t size)
{
    if (read_code(NVS_DEFAULT_PART_NAME, USER_NAMESPACE, key, out, size)) {
        return true;
    }
    return s_factory_available && read_code(FACTORY_PARTITION, FACTORY_NAMESPACE, key, out, size);
}

/* Compare without an early exit, so timing does not reveal how many leading digits match. */
static bool equal_constant_time(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < la && i < lb; i++) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static bool matches(const char *key, const char *code)
{
    char stored[MAX_CODE_LENGTH + 1];
    return effective_code(key, stored, sizeof(stored)) && equal_constant_time(stored, code);
}

bool codes_is_user(const char *code)
{
    return codes_is_valid(code, CONFIG_DB_USER_CODE_LENGTH) && matches(KEY_USER, code);
}

bool codes_is_admin(const char *code)
{
    return codes_is_valid(code, CONFIG_DB_ADMIN_CODE_LENGTH) && matches(KEY_ADMIN, code);
}

bool codes_is_valid(const char *text, int length)
{
    if ((int)strlen(text) != length) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

esp_err_t codes_set_user(const char *code)
{
    ESP_RETURN_ON_FALSE(codes_is_valid(code, CONFIG_DB_USER_CODE_LENGTH), ESP_ERR_INVALID_ARG, TAG, "invalid code");
    return db_nvs_set_str(USER_NAMESPACE, KEY_USER, code);
}

esp_err_t codes_set_admin(const char *code)
{
    ESP_RETURN_ON_FALSE(codes_is_valid(code, CONFIG_DB_ADMIN_CODE_LENGTH), ESP_ERR_INVALID_ARG, TAG, "invalid code");
    return db_nvs_set_str(USER_NAMESPACE, KEY_ADMIN, code);
}

esp_err_t codes_reset(void)
{
    return db_nvs_erase_namespace(USER_NAMESPACE);
}
