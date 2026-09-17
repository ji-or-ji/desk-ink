idf_component_register(
    SRCS "rtc.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES log
)
