idf_component_register(SRCS "db_proto.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES db_nvs esp_wifi mbedtls nvs_flash)
