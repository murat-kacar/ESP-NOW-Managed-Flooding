# Status LED patterns (SPEC 11) on espressif/led_indicator (dependency in idf_component.yml).
idf_component_register(SRCS "db_status_led.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_timer esp_driver_ledc)
