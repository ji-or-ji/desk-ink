idf_component_register(
    SRCS "app.c" "qemu_tour.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common eink ui input link store rtc power
    PRIV_REQUIRES log freertos
)

# 巡视用的真实照片裸位图（第三方素材，仅本地）。
# 文件缺失时回退到 qemu_tour.c 里的合成图，不报错。
set(_tour_data "${CMAKE_CURRENT_LIST_DIR}/../../testdata")
if(CONFIG_APP_QEMU_TOUR)
    if(EXISTS "${_tour_data}/art_full.bin" AND EXISTS "${_tour_data}/art_panel.bin")
        target_add_binary_data(${COMPONENT_LIB} "${_tour_data}/art_full.bin" BINARY)
        target_add_binary_data(${COMPONENT_LIB} "${_tour_data}/art_panel.bin" BINARY)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE APP_TOUR_HAS_PHOTO=1)
        message(STATUS "desk-ink tour: 使用 testdata/art_*.bin 真实照片")
    else()
        message(WARNING "desk-ink tour: testdata/art_*.bin 不存在，回退合成图")
    endif()
endif()
