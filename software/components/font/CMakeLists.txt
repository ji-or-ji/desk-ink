idf_component_register(
    SRCS "font.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES eink log
)

# 把点阵字库编进固件（XIP 直接读）
set(_font_dir "${CMAKE_CURRENT_LIST_DIR}/../../font")
target_add_binary_data(${COMPONENT_LIB} "${_font_dir}/font16_simsun.bin" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${_font_dir}/ascii8x16.bin" BINARY)
