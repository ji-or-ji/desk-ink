idf_component_register(
    SRCS "power.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_i2c
    PRIV_REQUIRES log
)
