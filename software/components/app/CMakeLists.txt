idf_component_register(
    SRCS "app.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common eink ui input link store
    PRIV_REQUIRES log freertos
)
