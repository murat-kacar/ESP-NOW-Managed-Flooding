idf_component_register(SRCS "main.c" "bell.c" "buttons.c" "heartbeat.c" "identity.c" "pairing.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES db_net db_nvs db_proto db_wdt db_status_led esp_driver_gpio esp_event esp_timer nvs_flash)
