idf_component_register(
    SRCS "app.c" "qemu_tour.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common eink ui input link store rtc power
    PRIV_REQUIRES log freertos
)
