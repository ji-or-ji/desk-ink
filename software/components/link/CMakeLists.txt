idf_component_register(
    SRCS "link.c" "link_json.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_driver_uart log freertos
)
