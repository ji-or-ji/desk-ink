#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "driver/i2c_master.h"

#include "eink.h"
#include "font.h"
#include "ui.h"
#include "store.h"
#include "rtc.h"
#include "power.h"
#include "app.h"

/*
 * 桌面墨水屏信息站 — 入口
 *
 * 只负责模块初始化，交互与渲染交给 app（components/app）。
 *
 * 硬件连接：
 *   GPIO 5 — I2C SDA (4.7kΩ 上拉到 3.3V)
 *   GPIO 6 — I2C SCL (4.7kΩ 上拉到 3.3V)
 */

#define I2C_SCL  GPIO_NUM_6
#define I2C_SDA  GPIO_NUM_5

static const char *TAG = "main";

/* 编进固件的字库（font 组件 target_add_binary_data 生成） */
extern const uint8_t font16_simsun_bin_start[] asm("_binary_font16_simsun_bin_start");
extern const uint8_t font16_simsun_bin_end[]   asm("_binary_font16_simsun_bin_end");
extern const uint8_t ascii8x16_bin_start[]     asm("_binary_ascii8x16_bin_start");
extern const uint8_t ascii8x16_bin_end[]       asm("_binary_ascii8x16_bin_end");

/* I2C 总线只建一次，rtc / power 共用 */
static i2c_master_bus_handle_t i2c_bus_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
    return bus;
}

static void i2c_scan(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "I2C scan (expect 0x68 DS3231, 0x4B MCP3021)...");
    int found = 0;
    for (uint8_t addr = 1; addr < 0x7F; addr++) {
        if (i2c_master_probe(bus, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  found 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGE(TAG, "no I2C devices, check wiring");
    } else {
        ESP_LOGI(TAG, "I2C scan done, %d device(s)", found);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "desk-ink booting");

    ESP_ERROR_CHECK(store_init());

    size_t cjk_len   = (size_t)(font16_simsun_bin_end - font16_simsun_bin_start);
    size_t ascii_len = (size_t)(ascii8x16_bin_end - ascii8x16_bin_start);
    ESP_ERROR_CHECK(font_init(font16_simsun_bin_start, cjk_len,
                              ascii8x16_bin_start, ascii_len));

    esp_err_t eink_err = eink_init();
    if (eink_err != ESP_OK) {
        ESP_LOGW(TAG, "eink_init failed (%s), continue without panel", esp_err_to_name(eink_err));
    }
    ESP_ERROR_CHECK(ui_init());

    i2c_master_bus_handle_t bus = i2c_bus_init();
    i2c_scan(bus);
    ds3231_init(bus);    power_init(bus);

    /* 启动时序：扫 LittleFS 的 img_*.bin 灌入内容池 */
    app_scan_images();

    ESP_ERROR_CHECK(app_start());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
