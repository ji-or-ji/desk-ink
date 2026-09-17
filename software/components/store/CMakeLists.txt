idf_component_register(
    SRCS "store.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common nvs_flash
    PRIV_REQUIRES joltwallet__littlefs log
)
