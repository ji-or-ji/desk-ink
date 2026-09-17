idf_component_register(
    SRCS "eink.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_driver_spi esp_driver_gpio heap freertos log
)
