idf_component_register(
    SRCS "ui.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common font eink
    PRIV_REQUIRES log
)
