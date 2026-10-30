idf_component_register(SRCS "main.c" "bell.c" "buttons.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES db_net db_status_led esp_driver_gpio esp_timer)
