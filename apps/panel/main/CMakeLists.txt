idf_component_register(SRCS "main.c" "codes.c" "door.c" "health.c" "input.c" "keypad.c" "pairing.c" "registry.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES db_net db_nvs db_proto db_status_led esp_driver_gpio esp_driver_ledc esp_timer
                                     nvs_flash)
