#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One-shot accessors for the default NVS partition. Each call opens the namespace, performs one operation,
 * commits a write and closes the handle. The NVS partition must be initialised (db_net_init() does it).
 *
 * Readers return ESP_ERR_NVS_NOT_FOUND when the namespace or the key does not exist.
 */

esp_err_t db_nvs_get_u8(const char *ns, const char *key, uint8_t *value);
esp_err_t db_nvs_set_u8(const char *ns, const char *key, uint8_t value);

esp_err_t db_nvs_get_u32(const char *ns, const char *key, uint32_t *value);
esp_err_t db_nvs_set_u32(const char *ns, const char *key, uint32_t value);

/** @brief Read a blob of exactly size bytes; ESP_ERR_INVALID_SIZE if the stored blob has another size. */
esp_err_t db_nvs_get_blob(const char *ns, const char *key, void *data, size_t size);
esp_err_t db_nvs_set_blob(const char *ns, const char *key, const void *data, size_t size);

esp_err_t db_nvs_set_str(const char *ns, const char *key, const char *value);

/** @brief Erase one key; a missing key is not an error. */
esp_err_t db_nvs_erase_key(const char *ns, const char *key);

/** @brief Erase every key of a namespace. */
esp_err_t db_nvs_erase_namespace(const char *ns);

#ifdef __cplusplus
}
#endif
