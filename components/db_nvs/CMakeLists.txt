# One-shot accessors for the default NVS partition (open, one operation, commit, close).
idf_component_register(SRCS "db_nvs.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash)
