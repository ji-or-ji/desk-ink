#include "power.h"

#include "esp_log.h"

static const char *TAG = "power";

#define MCP3021_ADDR 0x4B

/* 单节 LiPo：3.0V(0%) ~ 4.2V(100%) */
#define BAT_EMPTY_MV 3000
#define BAT_FULL_MV  4200

static i2c_master_dev_handle_t s_dev;

esp_err_t power_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MCP3021_ADDR,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        return err;
    }
    if (i2c_master_probe(bus, MCP3021_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "MCP3021 not responding at 0x%02X", MCP3021_ADDR);
    }
    ESP_LOGI(TAG, "MCP3021 ready");
    return ESP_OK;
}

esp_err_t power_read_mv(int *mv_out)
{
    if (mv_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t b[2];
    esp_err_t err = i2c_master_receive(s_dev, b, sizeof(b), 100);
    if (err != ESP_OK) {
        return err;
    }
    /* 10 位值在高 10 位；VDD=3.3V 参考，输入经 100k+100k 分压，故乘 2 */
    uint16_t code = (uint16_t)(((uint16_t)b[0] << 2) | (b[1] >> 6));
    *mv_out = (int)((int32_t)code * 6600 / 1023);
    return ESP_OK;
}

int power_percent(int mv)
{
    if (mv <= BAT_EMPTY_MV) {
        return 0;
    }
    if (mv >= BAT_FULL_MV) {
        return 100;
    }
    return (mv - BAT_EMPTY_MV) * 100 / (BAT_FULL_MV - BAT_EMPTY_MV);
}
