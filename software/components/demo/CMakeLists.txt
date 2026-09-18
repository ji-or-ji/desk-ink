idf_component_register(
    SRCS "demo.c" "demo_source_file.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common eink store
    PRIV_REQUIRES freertos log esp_timer
)

# QEMU / 无推流链路时，把 tools/badapple 生成的帧包编进固件，
# 启动时复制到 LittleFS，用于验证播放逻辑。文件缺失时只告警，不报错。
if(CONFIG_DEMO_EMBED_BUNDLE)
    set(_baf "${CMAKE_CURRENT_LIST_DIR}/../../testdata/badapple.baf")
    if(EXISTS "${_baf}")
        target_add_binary_data(${COMPONENT_LIB} "${_baf}" BINARY)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE DEMO_HAS_BUNDLE=1)
        message(STATUS "desk-ink demo: 内嵌 testdata/badapple.baf")
    else()
        message(WARNING "desk-ink demo: testdata/badapple.baf 不存在，无法内嵌")
    endif()
endif()
