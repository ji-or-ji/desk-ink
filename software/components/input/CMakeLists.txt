idf_component_register(
    SRCS "input.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_adc esp_driver_gpio esp_hw_support freertos log
)
